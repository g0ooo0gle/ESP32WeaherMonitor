/**
 * ESP32 Weather Station - ニュース画面 実装
 *
 * [画面レイアウト]
 *
 *   ┌──────────────────────────────┐  Y=  0
 *   │ ニュース            3/12     │  タイトルバー (20px)
 *   ├──────────────────────────────┤  Y= 20
 *   │                              │
 *   │  見出しを折り返し静止表示     │  静止エリア (90px、最大5行)
 *   │  (12px フォント)             │
 *   │                              │
 *   ├──────────────────────────────┤  Y=112
 *   │ ▶ テキストが右→左へ流れる   │  電光掲示板エリア (46px)
 *   └──────────────────────────────┘  Y=160
 *
 * [自動遷移のバグ対策]
 *   scrollX のリセットは drawNewsScreen/redrawNewsContent を呼ぶ前に行う。
 *   これにより、内部の Mutex 取得が遅延しても二重トリガーが起きない。
 *   自動遷移には軽量な redrawNewsContent() を使い fillScreen を省く。
 */

#include "ticker.h"
#include "display.h"
#include "button.h"
#include <WiFi.h>
#include <HTTPClient.h>

// ================================================================
// 共有データ (Core 0 タスクとメイン Core 1 の両方からアクセス)
// ================================================================
static char newsItems[NEWS_MAX_ITEMS][NEWS_TITLE_MAX];
static int  newsCount = 0;

static SemaphoreHandle_t newsMutex = nullptr;

static int newsIndex = 0;

// ================================================================
// 電光掲示板スクロール状態
// ================================================================
static int           scrollX      = SCREEN_W; // 現在のスクロール X 座標
static int           scrollTextW  = 0;         // 見出し全幅 (16px フォントで計測)
static unsigned long lastScrollMs = 0;

// スクロール用ローカルコピー (mutex なしで描画できるようにするため)
// initScroll() 時に newsItems[newsIndex] をコピーし、Phase 1 はこれを使う。
// RSS 書き込み中でも ticker が止まらない。
static char currentScrollText[NEWS_TITLE_MAX] = "";

static bool          waitingForNews   = false;
static unsigned long scrollPauseEndMs = 0;  // 0 = ポーズなし

static const int           SCROLL_SPEED_PX     = 2;     // px / ティック
static const int           SCROLL_TICK_MS       = 40;   // ms / ティック ≒ 25fps (負荷軽減)
static const unsigned long TRANSITION_PAUSE_MS  = 800UL; // ページ切替前の停止時間

// 電光掲示板エリアのレイアウト (16px フォント, NEWS_TICKER_Y=114, NEWS_TICKER_H=46)
static const int TICKER_BASELINE = NEWS_TICKER_Y + NEWS_TICKER_H / 2 + 8;  // = 145
static const int TICKER_CLEAR_Y  = TICKER_BASELINE - 16;                    // = 129
static const int TICKER_CLEAR_H  = 24;

// ================================================================
// [内部] UTF-8 文字のバイト数を返す (1〜4)
// ================================================================
static inline int utf8SeqLen(unsigned char c)
{
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

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
// [内部] XML から <title> タグを抽出して newsItems[] に格納
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

    if (titleCount > 0)  // 0 件目はチャンネル名
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
// [内部] NHK RSS を取得 (Core 0 タスクから呼ぶ)
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

  if (httpCode == HTTP_CODE_OK) {
    String xml = http.getString();
    Serial.printf("[News] RSS取得完了 (%d bytes)\n", xml.length());

    if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
      extractTitlesFromRss(xml);
      xSemaphoreGive(newsMutex);
      Serial.printf("[News] 見出し %d件 取得\n", newsCount);
    } else {
      Serial.println(F("[News] Mutex取得失敗、今回はスキップ"));
    }
  } else {
    Serial.printf("[News] HTTP失敗 code=%d\n", httpCode);
  }

  http.end();
}

// ================================================================
// FreeRTOS タスク: Core 0 で RSS を 5 分ごとに取得
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
// [内部] 静止エリア: 12px フォントで折り返し多行描画 (最大 5 行)
// ================================================================
static void drawNewsBody(const char *text)
{
  tft.fillRect(0, NEWS_STATIC_Y, SCREEN_W, NEWS_STATIC_H, COL_BG_NEWS);

  u8g2.setFontMode(1);
  u8g2.setFont(u8g2_font_b12_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setBackgroundColor(COL_BG_NEWS);

  const int lineH    = 16;
  const int maxLines = NEWS_STATIC_H / lineH;  // 90/16 = 5
  const int leftPad  = 4;
  const int bodyW    = SCREEN_W - leftPad * 2;

  char lineBuf[NEWS_TITLE_MAX];
  int  lineLen = 0, lineCount = 0, i = 0;

  while (text[i] != '\0' && lineCount < maxLines) {
    int seqLen = utf8SeqLen((unsigned char)text[i]);

    if (lineLen + seqLen >= (int)sizeof(lineBuf) - 1) break;
    for (int k = 0; k < seqLen; k++) lineBuf[lineLen + k] = text[i + k];
    lineBuf[lineLen + seqLen] = '\0';

    if (u8g2.getUTF8Width(lineBuf) > bodyW) {
      lineBuf[lineLen] = '\0';
      u8g2.setCursor(leftPad, NEWS_STATIC_Y + lineH * (lineCount + 1) - 2);
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

  if (lineLen > 0 && lineCount < maxLines) {
    lineBuf[lineLen] = '\0';
    u8g2.setCursor(leftPad, NEWS_STATIC_Y + lineH * (lineCount + 1) - 2);
    u8g2.print(lineBuf);
  }
}

// ================================================================
// [内部] 電光掲示板エリア: 16px フォントで横スクロール描画
//
// テキスト行 1 行分だけクリアして再描画 (エリア全体より高速)。
// 可視範囲外の文字は幅計算のみでレンダリングをスキップ。
// ================================================================
static void drawScrollBody(const char *text)
{
  tft.fillRect(0, TICKER_CLEAR_Y, SCREEN_W, TICKER_CLEAR_H, COL_BG_NEWS);

  u8g2.setFontMode(1);
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setBackgroundColor(COL_BG_NEWS);

  int x = scrollX;
  int i = 0;

  while (text[i] != '\0') {
    int seqLen = utf8SeqLen((unsigned char)text[i]);

    char tmp[8] = {};
    for (int k = 0; k < seqLen; k++) tmp[k] = text[i + k];
    int cw = u8g2.getUTF8Width(tmp);

    if (x + cw > 0) {
      if (x >= SCREEN_W) break;
      u8g2.setCursor(x, TICKER_BASELINE);
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
//
// newsItems[newsIndex] を currentScrollText にコピーする。
// 以降の drawScrollBody() は currentScrollText を使うため、
// mutex なしで安全に呼べる (RSS 書き込み中でも止まらない)。
// ================================================================
static void initScroll()
{
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  scrollTextW = u8g2.getUTF8Width(newsItems[newsIndex]);
  strncpy(currentScrollText, newsItems[newsIndex], NEWS_TITLE_MAX - 1);
  currentScrollText[NEWS_TITLE_MAX - 1] = '\0';
  // scrollX は呼び出し前に SCREEN_W にリセット済みであること
  lastScrollMs = millis();
}

// ================================================================
// [内部] ページ遷移時の画面更新 (mutex 保持状態で呼ぶこと)
//
// mutex を外から取得済みの前提で描画する。
// initScroll() がここで呼ばれるため scrollTextW/currentScrollText が必ず確定する。
// ================================================================
static void drawNewsPage()
{
  tft.fillRect(0, NEWS_STATIC_Y, SCREEN_W, NEWS_STATIC_H, COL_BG_NEWS);
  tft.fillRect(0, NEWS_TICKER_Y, SCREEN_W, NEWS_TICKER_H, COL_BG_NEWS);
  tft.drawFastHLine(0, NEWS_DIVIDER_Y, SCREEN_W, ST77XX_WHITE);
  drawNewsTitleBar();
  if (newsCount > 0) {
    drawNewsBody(newsItems[newsIndex]);
    scrollX = SCREEN_W;
    initScroll();
    drawScrollBody(currentScrollText);
  }
}

// ================================================================
// ニュース画面を描画する (公開API)
// 画面切替・ボタン操作時に呼ぶ。fillScreen で完全リセット。
// ================================================================
void drawNewsScreen()
{
  tft.fillScreen(COL_BG_NEWS);
  drawNewsTitleBar();
  tft.drawFastHLine(0, NEWS_DIVIDER_Y, SCREEN_W, ST77XX_WHITE);

  if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    if (newsCount == 0) {
      waitingForNews = true;
      u8g2.setFontMode(1);
      u8g2.setFont(u8g2_font_b12_t_japanese3);
      u8g2.setForegroundColor(ST77XX_WHITE);
      u8g2.setBackgroundColor(COL_BG_NEWS);
      u8g2.setCursor(8, NEWS_STATIC_Y + 30);
      u8g2.print("ニュース取得中...");
    } else {
      waitingForNews = false;
      scrollPauseEndMs = 0;
      if (newsIndex < 0)          newsIndex = newsCount - 1;
      if (newsIndex >= newsCount) newsIndex = 0;

      scrollX = SCREEN_W;
      drawNewsBody(newsItems[newsIndex]);
      initScroll();                            // currentScrollText も更新
      drawScrollBody(currentScrollText);
    }
    xSemaphoreGive(newsMutex);
  }
}

// ================================================================
// スクロール更新 (loop() からニュース画面表示中のみ毎回呼ぶ)
//
// [3 フェーズ設計 + mutex 安全設計]
//
//   Phase 0: 初回データ到着待ち
//     waitingForNews && newsCount > 0 のとき。
//     mutex を短いタイムアウトで試み、取れなければ次の呼び出しで再試行。
//
//   Phase 1: スクロール進行中 (scrollPauseEndMs == 0)
//     currentScrollText を使って mutex なしで描画 → RSS 取得中でも止まらない。
//     scrollX が -scrollTextW を下回ったらティッカーをクリアして Phase 2 へ。
//
//   Phase 2: ポーズ中 (scrollPauseEndMs != 0 && now < scrollPauseEndMs)
//     何もせず return。
//
//   Phase 3: ポーズ完了 (scrollPauseEndMs != 0 && now >= scrollPauseEndMs)
//     mutex を取れなければ 200ms 延長して再試行 (newsIndex は変えない)。
//     mutex 取得成功後に newsIndex を進めて drawNewsPage() を呼ぶ。
//     → initScroll() が必ず mutex 内で呼ばれるため scrollTextW は常に正確。
// ================================================================
void updateNewsAutoPaging()
{
  unsigned long now = millis();

  // ---- Phase 0: 初回データ到着 -------------------------------------------
  if (waitingForNews && newsCount > 0) {
    if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    waitingForNews   = false;
    scrollPauseEndMs = 0;
    newsIndex        = 0;
    drawNewsPage();                      // mutex 内で scrollX/scrollTextW を確定
    xSemaphoreGive(newsMutex);
    return;
  }

  if (newsCount == 0) return;

  // ---- Phase 2 / 3: ポーズ中 / 遷移 ----------------------------------------
  if (scrollPauseEndMs != 0) {
    if (now < scrollPauseEndMs) return;  // Phase 2: まだ待機中

    // Phase 3: RSS 取得が mutex を保持中なら延長して再試行
    if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
      scrollPauseEndMs = now + 200;      // 200ms 後に再挑戦
      return;
    }
    // mutex 取得成功 → newsIndex 進めて確実に再描画
    scrollPauseEndMs = 0;
    newsIndex        = (newsIndex + 1) % newsCount;
    drawNewsPage();                      // initScroll() は内部で呼ばれる
    xSemaphoreGive(newsMutex);
    return;
  }

  // ---- Phase 1: スクロール進行中 --------------------------------------------
  if (now - lastScrollMs < SCROLL_TICK_MS) return;
  lastScrollMs = now;

  scrollX -= SCROLL_SPEED_PX;

  if (scrollX < -scrollTextW) {
    tft.fillRect(0, NEWS_TICKER_Y, SCREEN_W, NEWS_TICKER_H, COL_BG_NEWS);
    scrollPauseEndMs = now + TRANSITION_PAUSE_MS;
    return;
  }

  // 通常フレーム: currentScrollText を使って mutex なしで描画
  drawScrollBody(currentScrollText);
}

// ================================================================
// 次の見出しへ手動で進める (NEXT 短押し)
// ================================================================
void nextNewsPage()
{
  if (newsCount == 0) return;
  scrollPauseEndMs = 0;
  newsIndex        = (newsIndex + 1) % newsCount;
  scrollX          = SCREEN_W;
  drawNewsScreen();
}

// ================================================================
// 前の見出しへ戻す
// ================================================================
void prevNewsPage()
{
  if (newsCount == 0) return;
  scrollPauseEndMs = 0;
  newsIndex        = (newsIndex - 1 + newsCount) % newsCount;
  scrollX          = SCREEN_W;
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

  newsCount             = 0;
  scrollX               = SCREEN_W;
  scrollTextW           = 0;
  currentScrollText[0]  = '\0';
  waitingForNews        = false;
  lastScrollMs          = 0;
  scrollPauseEndMs      = 0;

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
