/**
 * ESP32 Weather Station - ニュース画面 実装
 *
 * [表示フロー]
 *   1. STATIC フェーズ (30秒):
 *        12px フォントで折り返し多行表示。見出しを静止して読む。
 *        長い見出しは末尾が切れることがあるが、次フェーズで補完。
 *   2. SCROLL フェーズ:
 *        16px フォントで電光掲示板スタイルの横スクロール。
 *        全文が流れ終わったら次の見出しの STATIC フェーズへ移行。
 *
 * [利点]
 *   ・短い見出し: 30秒で全文一覧 → スクロールで再確認
 *   ・長い見出し: 切れた部分もスクロールで確認可能
 *   ・スクロール中は文字が大きく読みやすい
 *
 * [タスク間データ共有]
 *   newsItems[]  : 取得済みの見出し配列 (共有)
 *   newsCount    : 取得済み件数 (共有)
 *   newsMutex    : 上記の保護
 */

#include "ticker.h"
#include "display.h"
#include "button.h"
#include <WiFi.h>
#include <HTTPClient.h>

// ================================================================
// 共有データ (タスクとメインの両方からアクセス)
// ================================================================
static char newsItems[NEWS_MAX_ITEMS][NEWS_TITLE_MAX];
static int  newsCount = 0;

static SemaphoreHandle_t newsMutex = nullptr;

static int newsIndex = 0;

// ================================================================
// 表示フェーズ管理
// ================================================================
enum class NewsPhase { STATIC, SCROLL };

static NewsPhase     displayPhase   = NewsPhase::STATIC;
static unsigned long staticStartMs  = 0;      // STATIC フェーズ開始時刻
static bool          waitingForNews = false;  // ニュース取得待ちフラグ

// 静止表示時間: 30 秒
static const unsigned long STATIC_DURATION_MS = 30000UL;

// ================================================================
// スクロール状態 (SCROLL フェーズのみで使用)
// ================================================================
static int           scrollX      = 0;   // 現在のスクロール X 座標 (px)
static int           scrollTextW  = 0;   // 見出し全幅 (16px フォントで計測)
static unsigned long lastScrollMs = 0;   // 前回スクロール更新時刻

static const int SCROLL_SPEED_PX = 2;   // 1 ティックあたりの移動量 (px)
static const int SCROLL_TICK_MS  = 30;  // スクロール更新間隔 (ms) ≒ 33fps

// スクロールフェーズのレイアウト (16px フォント、本文エリア縦中央)
//   NEWS_BODY_Y=22, NEWS_BODY_H=138 → 中央 Y=91 → ベースライン Y=99
static const int SCROLL_Y_BASELINE = NEWS_BODY_Y + NEWS_BODY_H / 2 + 8;  // = 99
static const int SCROLL_CLEAR_TOP  = SCROLL_Y_BASELINE - 16;              // = 83
static const int SCROLL_CLEAR_H    = 26;                                  // px

// ================================================================
// [内部] UTF-8 として不正なバイト列を '?' に置換
// ================================================================
static void sanitizeUtf8(char *buf)
{
  int i = 0;
  while (buf[i]) {
    unsigned char c = (unsigned char)buf[i];
    int seqLen = 0;
    if      ((c & 0x80) == 0x00) seqLen = 1;
    else if ((c & 0xE0) == 0xC0) seqLen = 2;
    else if ((c & 0xF0) == 0xE0) seqLen = 3;
    else if ((c & 0xF8) == 0xF0) seqLen = 4;
    else { buf[i] = '?'; i++; continue; }

    bool ok = true;
    for (int k = 1; k < seqLen; k++) {
      unsigned char cc = (unsigned char)buf[i + k];
      if (cc == 0 || (cc & 0xC0) != 0x80) { ok = false; break; }
    }
    if (!ok) { buf[i] = '?'; i++; }
    else      i += seqLen;
  }
}

// ================================================================
// [内部] XML から <title> タグを順番に抽出して newsItems[] に格納
// ================================================================
static void extractTitlesFromRss(const String &xml)
{
  newsCount = 0;
  int searchFrom = 0;
  int titleCount = 0;

  while (newsCount < NEWS_MAX_ITEMS)
  {
    int start = xml.indexOf("<title>", searchFrom);
    if (start == -1) break;
    start += 7;

    int end = xml.indexOf("</title>", start);
    if (end == -1) break;

    if (titleCount > 0)   // 0 件目はチャンネル名なのでスキップ
    {
      String title = xml.substring(start, end);
      title.replace("&amp;",  "&");
      title.replace("&lt;",   "<");
      title.replace("&gt;",   ">");
      title.replace("&quot;", "\"");
      title.replace("&apos;", "'");
      title.replace("<![CDATA[", "");
      title.replace("]]>",        "");
      title.trim();

      if (title.length() > 0) {
        strncpy(newsItems[newsCount], title.c_str(), NEWS_TITLE_MAX - 1);
        newsItems[newsCount][NEWS_TITLE_MAX - 1] = '\0';
        sanitizeUtf8(newsItems[newsCount]);
        newsCount++;
      }
    }

    titleCount++;
    searchFrom = end + 8;
  }
}

// ================================================================
// [内部・タスクから呼ぶ] NHK RSS を取得
// ================================================================
static void fetchNewsImpl()
{
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[News] WiFi未接続のためスキップします。"));
    return;
  }

  const char *rssUrl = "https://www3.nhk.or.jp/rss/news/cat0.xml";
  HTTPClient http;
  http.setTimeout(8000);
  http.begin(rssUrl);
  http.addHeader("Accept-Charset", "utf-8");

  Serial.println(F("[News] NHK RSS取得中..."));
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK)
  {
    String xml = http.getString();
    Serial.printf("[News] RSS取得完了 (%d bytes)\n", xml.length());

    if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
      extractTitlesFromRss(xml);
      xSemaphoreGive(newsMutex);
      Serial.printf("[News] 見出し %d件 取得\n", newsCount);
    } else {
      Serial.println(F("[News] Mutex取得失敗、今回はスキップ"));
    }
  }
  else {
    Serial.printf("[News] HTTP失敗 code=%d\n", httpCode);
  }

  http.end();
}

// ================================================================
// FreeRTOS タスク: Core 0 で動作する RSS 取得ループ (5分ごと)
// ================================================================
static void newsFetchTask(void * /*param*/)
{
  fetchNewsImpl();

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5UL * 60UL * 1000UL));
    fetchNewsImpl();
  }
}

// ================================================================
// [内部] STATIC フェーズ: 12px フォントで折り返し多行描画
//
// 1文字ずつ行バッファに追加し、幅が bodyW を超えたら改行。
// maxLines 行まで描画し、溢れた行は次のスクロールフェーズで補完。
// ================================================================
static void drawNewsBody(const char *text)
{
  // 本文エリア全体をクリア
  tft.fillRect(0, NEWS_BODY_Y, SCREEN_W, NEWS_BODY_H, COL_BG_NEWS);

  u8g2.setFontMode(1);
  u8g2.setFont(u8g2_font_b12_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setBackgroundColor(COL_BG_NEWS);

  const int lineH    = 16;                   // フォント 12px + 行間 4px
  const int maxLines = NEWS_BODY_H / lineH;  // 138/16 = 8 行
  const int leftPad  = 4;
  const int bodyW    = SCREEN_W - leftPad * 2;

  char lineBuf[NEWS_TITLE_MAX];
  int  lineLen   = 0;
  int  lineCount = 0;
  int  i = 0;

  while (text[i] != '\0' && lineCount < maxLines) {
    unsigned char c = (unsigned char)text[i];
    int seqLen = 1;
    if      ((c & 0xE0) == 0xC0) seqLen = 2;
    else if ((c & 0xF0) == 0xE0) seqLen = 3;
    else if ((c & 0xF8) == 0xF0) seqLen = 4;

    if (lineLen + seqLen >= (int)sizeof(lineBuf) - 1) break;
    for (int k = 0; k < seqLen; k++) lineBuf[lineLen + k] = text[i + k];
    lineBuf[lineLen + seqLen] = '\0';

    int w = u8g2.getUTF8Width(lineBuf);
    if (w > bodyW) {
      // 幅オーバー: 現在行を確定して次の行へ
      lineBuf[lineLen] = '\0';
      u8g2.setCursor(leftPad, NEWS_BODY_Y + lineH * (lineCount + 1) - 2);
      u8g2.print(lineBuf);
      lineCount++;

      lineLen = 0;
      if (lineCount < maxLines) {
        for (int k = 0; k < seqLen; k++) lineBuf[lineLen + k] = text[i + k];
        lineLen += seqLen;
        lineBuf[lineLen] = '\0';
      }
    } else {
      lineLen += seqLen;
    }
    i += seqLen;
  }

  // 末尾の残り行を描画
  if (lineLen > 0 && lineCount < maxLines) {
    lineBuf[lineLen] = '\0';
    u8g2.setCursor(leftPad, NEWS_BODY_Y + lineH * (lineCount + 1) - 2);
    u8g2.print(lineBuf);
  }
}

// ================================================================
// [内部] SCROLL フェーズ: 16px フォントで電光掲示板スタイル描画
//
// テキスト行 1 行分 (SCROLL_CLEAR_H px) だけクリアして効率化。
// 可視範囲外の文字は幅計算のみ行いレンダリングをスキップ。
// 左端にかかる文字は Adafruit_GFX がピクセル単位でクリップする。
// ================================================================
static void drawScrollBody(const char *text)
{
  tft.fillRect(0, SCROLL_CLEAR_TOP, SCREEN_W, SCROLL_CLEAR_H, COL_BG_NEWS);

  u8g2.setFontMode(1);
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setBackgroundColor(COL_BG_NEWS);

  int x = scrollX;
  int i = 0;

  while (text[i] != '\0') {
    unsigned char c = (unsigned char)text[i];
    int seqLen = 1;
    if      ((c & 0xE0) == 0xC0) seqLen = 2;
    else if ((c & 0xF0) == 0xE0) seqLen = 3;
    else if ((c & 0xF8) == 0xF0) seqLen = 4;

    char tmp[8] = {};
    for (int k = 0; k < seqLen; k++) tmp[k] = text[i + k];
    int cw = u8g2.getUTF8Width(tmp);

    if (x + cw > 0) {
      if (x >= SCREEN_W) break;   // 右端を超えたら以降は不要
      u8g2.setCursor(x, SCROLL_Y_BASELINE);
      u8g2.print(tmp);
    }

    x += cw;
    i += seqLen;
  }
}

// ================================================================
// [内部] タイトルバーを描画 ("ニュース 3/12")
// ================================================================
static void drawNewsTitleBar()
{
  tft.fillRect(0, NEWS_TITLE_Y, SCREEN_W, NEWS_TITLE_H, COL_BG_NEWSBAR);

  u8g2.setFontMode(1);
  u8g2.setFont(u8g2_font_b12_t_japanese3);
  u8g2.setForegroundColor(ST77XX_YELLOW);
  u8g2.setCursor(4, NEWS_TITLE_Y + 14);
  u8g2.print("ニュース");

  if (newsCount > 0) {
    char counter[16];
    snprintf(counter, sizeof(counter), "%d/%d", newsIndex + 1, newsCount);
    u8g2.setFont(u8g2_font_helvB10_tf);
    u8g2.setForegroundColor(ST77XX_WHITE);
    int cw = u8g2.getUTF8Width(counter);
    u8g2.setCursor(SCREEN_W - cw - 4, NEWS_TITLE_Y + 14);
    u8g2.print(counter);
  }

  tft.drawFastHLine(0, NEWS_LINE_Y, SCREEN_W, ST77XX_WHITE);
}

// ================================================================
// [内部] スクロール状態を初期化 (Mutex 保持状態で呼ぶこと)
// 16px フォントで全幅を計測し、右端から開始
// ================================================================
static void initScroll()
{
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  scrollTextW  = u8g2.getUTF8Width(newsItems[newsIndex]);
  scrollX      = SCREEN_W;
  lastScrollMs = millis();
}

// ================================================================
// ニュース画面を描画する (公開API)
// 呼び出すと常に STATIC フェーズから開始する。
// ================================================================
void drawNewsScreen()
{
  tft.fillScreen(COL_BG_NEWS);
  drawNewsTitleBar();

  displayPhase  = NewsPhase::STATIC;
  staticStartMs = millis();

  if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    if (newsCount == 0) {
      waitingForNews = true;
      u8g2.setFontMode(1);
      u8g2.setFont(u8g2_font_b12_t_japanese3);
      u8g2.setForegroundColor(ST77XX_WHITE);
      u8g2.setBackgroundColor(COL_BG_NEWS);
      u8g2.setCursor(8, NEWS_BODY_Y + 30);
      u8g2.print("ニュース取得中...");
    } else {
      waitingForNews = false;
      if (newsIndex < 0)          newsIndex = newsCount - 1;
      if (newsIndex >= newsCount) newsIndex = 0;
      drawNewsBody(newsItems[newsIndex]);
    }
    xSemaphoreGive(newsMutex);
  }
}

// ================================================================
// 表示更新 (loop() からニュース画面表示中のみ毎回呼ぶ)
//
// ─ STATIC フェーズ ─────────────────────────────────────────────
//   折り返し多行テキストを 30 秒間静止表示。
//   経過後、本文エリアをクリアして SCROLL フェーズへ移行。
//
// ─ SCROLL フェーズ ─────────────────────────────────────────────
//   16px フォントで横スクロール (≒33fps)。
//   テキストが左端を抜けたら次の見出しの STATIC フェーズへ移行。
// ================================================================
void updateNewsAutoPaging()
{
  unsigned long now = millis();

  // ─── ニュースが初めて届いたとき (取得中 → 実データへ切替) ────
  if (waitingForNews && newsCount > 0) {
    waitingForNews = false;
    newsIndex      = 0;
    displayPhase   = NewsPhase::STATIC;
    staticStartMs  = now;
    tft.fillRect(0, NEWS_BODY_Y, SCREEN_W, NEWS_BODY_H, COL_BG_NEWS);
    if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      drawNewsBody(newsItems[newsIndex]);
      xSemaphoreGive(newsMutex);
    }
    drawNewsTitleBar();
    return;
  }

  if (newsCount == 0) return;

  // ─── STATIC フェーズ ──────────────────────────────────────────
  if (displayPhase == NewsPhase::STATIC) {
    if (now - staticStartMs < STATIC_DURATION_MS) return;

    // 30 秒経過 → SCROLL フェーズへ
    displayPhase = NewsPhase::SCROLL;
    tft.fillRect(0, NEWS_BODY_Y, SCREEN_W, NEWS_BODY_H, COL_BG_NEWS);
    if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      initScroll();
      drawScrollBody(newsItems[newsIndex]);
      xSemaphoreGive(newsMutex);
    }
    return;
  }

  // ─── SCROLL フェーズ ──────────────────────────────────────────
  if (now - lastScrollMs < SCROLL_TICK_MS) return;
  lastScrollMs = now;

  scrollX -= SCROLL_SPEED_PX;

  // テキストが完全に左端を抜けたら次の見出し → STATIC へ
  if (scrollX < -scrollTextW) {
    newsIndex     = (newsIndex + 1) % newsCount;
    displayPhase  = NewsPhase::STATIC;
    staticStartMs = now;

    tft.fillRect(0, NEWS_BODY_Y, SCREEN_W, NEWS_BODY_H, COL_BG_NEWS);
    if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      drawNewsBody(newsItems[newsIndex]);
      xSemaphoreGive(newsMutex);
    }
    drawNewsTitleBar();
    return;
  }

  // 通常スクロールフレーム: テキスト行のみ再描画
  if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    drawScrollBody(newsItems[newsIndex]);
    xSemaphoreGive(newsMutex);
  }
}

// ================================================================
// 次の見出しへ手動で進める (NEXT 短押し)
// STATIC フェーズから開始する。
// ================================================================
void nextNewsPage()
{
  if (newsCount == 0) return;
  newsIndex = (newsIndex + 1) % newsCount;
  drawNewsScreen();
}

// ================================================================
// 前の見出しへ戻す
// ================================================================
void prevNewsPage()
{
  if (newsCount == 0) return;
  newsIndex = (newsIndex - 1 + newsCount) % newsCount;
  drawNewsScreen();
}

// ================================================================
// ニュース機能の初期化 (setup() で1回)
// ================================================================
void setupNews()
{
  newsMutex = xSemaphoreCreateMutex();
  if (newsMutex == nullptr) {
    Serial.println(F("[News] Mutex作成失敗"));
    return;
  }

  newsCount      = 0;
  scrollX        = 0;
  scrollTextW    = 0;
  waitingForNews = false;

  for (int i = 0; i < NEWS_MAX_ITEMS; i++) {
    newsItems[i][0] = '\0';
  }

  xTaskCreatePinnedToCore(
    newsFetchTask,
    "newsFetch",
    8192,
    nullptr,
    1,
    nullptr,
    0
  );

  Serial.println(F("[News] setupNews() 完了。Core0タスク起動済み。"));
}
