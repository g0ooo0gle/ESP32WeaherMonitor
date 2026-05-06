/**
 * ticker.cpp - ニュース画面実装
 *
 * [画面構成]
 *   静止エリア  : 見出し（タイトル）を折り返し多行表示
 *   スクロール  : 記事概要（ディスクリプション）を電光掲示板スクロール
 *   概要が空の場合はタイトルをフォールバックとして使用します。
 *
 * [マルチコア安全設計]
 *   newsItems / newsDescriptions の読み書きは newsMutex で保護します。
 *   currentScrollText は initScroll() 時に mutex 内でコピーし、
 *   以降の描画は mutex なしで安全に行います。
 *
 * [自動ページ送りの動作]
 *   スクロールが 1 周完了 → TRANSITION_PAUSE_MS 待機 → 次ページへ
 */

#include "ticker.h"
#include "display.h"
#include "button.h"
#include "settings.h"
#include <WiFi.h>
#include <HTTPClient.h>

// ================================================================
// 共有データ
// ================================================================
static char         newsItems[NEWS_MAX_ITEMS][NEWS_TITLE_MAX];
static char         newsDescriptions[NEWS_MAX_ITEMS][NEWS_DESC_MAX];
static volatile int newsCount = 0;

static SemaphoreHandle_t newsMutex = nullptr;

static int newsIndex = 0;

// ================================================================
// 電光掲示板スクロール状態
// ================================================================
static int           scrollX      = SCREEN_W;
static int           scrollTextW  = 0;
static unsigned long lastScrollMs = 0;

// RSS 書き込み中でも安全に描画できるよう、スクロールテキストはローカルコピーを使用
static char currentScrollText[NEWS_DESC_MAX] = "";

static bool          waitingForNews   = false;
static unsigned long scrollPauseEndMs = 0;

static const int           SCROLL_SPEED_PX    = 2;
static const int           SCROLL_TICK_MS     = 40;    // ≒ 25fps
static const unsigned long TRANSITION_PAUSE_MS = 800UL;

static const int TICKER_BASELINE = NEWS_TICKER_Y + NEWS_TICKER_H / 2 + 8;
static const int TICKER_CLEAR_Y  = TICKER_BASELINE - 16;
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
    int seqLen;
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
// [内部] RSS フィールドをクリーンアップして返す
//   1. CDATA ラッパー除去
//   2. HTML タグ除去
//   3. HTML エンティティデコード
// ================================================================
static String cleanRssField(const String &raw)
{
  String s = raw;
  s.replace("<![CDATA[", "");
  s.replace("]]>",       "");

  String clean;
  clean.reserve(s.length());
  bool inTag = false;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if      (c == '<') { inTag = true;  }
    else if (c == '>') { inTag = false; }
    else if (!inTag)   { clean += c;    }
  }

  clean.replace("&amp;",  "&");
  clean.replace("&lt;",   "<");
  clean.replace("&gt;",   ">");
  clean.replace("&quot;", "\"");
  clean.replace("&apos;", "'");
  clean.replace("&#160;", " ");
  clean.trim();
  return clean;
}

// ================================================================
// [内部] <item> ブロック単位で title + description を抽出
// チャンネルレベルの <title> を誤って取り込まないよう
// <item>...</item> を繰り返し検索します。
// ================================================================
static void extractItemsFromRss(const String &xml)
{
  newsCount = 0;
  int searchFrom = 0;

  while (newsCount < NEWS_MAX_ITEMS) {
    int itemStart = xml.indexOf("<item>", searchFrom);
    if (itemStart == -1) break;
    int itemEnd = xml.indexOf("</item>", itemStart);
    if (itemEnd == -1) break;

    String item = xml.substring(itemStart + 6, itemEnd);

    int ts = item.indexOf("<title>");
    int te = item.indexOf("</title>");
    if (ts == -1 || te == -1 || te <= ts) { searchFrom = itemEnd + 7; continue; }
    String title = cleanRssField(item.substring(ts + 7, te));
    if (title.length() == 0) { searchFrom = itemEnd + 7; continue; }

    int ds = item.indexOf("<description>");
    int de = item.indexOf("</description>");
    String desc = "";
    if (ds != -1 && de != -1 && de > ds) {
      desc = cleanRssField(item.substring(ds + 13, de));
    }
    if (desc.length() == 0) desc = title;

    strncpy(newsItems[newsCount], title.c_str(), NEWS_TITLE_MAX - 1);
    newsItems[newsCount][NEWS_TITLE_MAX - 1] = '\0';
    sanitizeUtf8(newsItems[newsCount]);

    strncpy(newsDescriptions[newsCount], desc.c_str(), NEWS_DESC_MAX - 1);
    newsDescriptions[newsCount][NEWS_DESC_MAX - 1] = '\0';
    sanitizeUtf8(newsDescriptions[newsCount]);

    newsCount++;
    searchFrom = itemEnd + 7;
  }
}

// ================================================================
// [内部] NHK RSS を取得する（Core 0 タスクから呼ぶ）
// ================================================================
static void fetchNewsImpl()
{
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[News] WiFi 未接続のためスキップ"));
    return;
  }

  HTTPClient http;
  http.setTimeout(8000);
  http.begin(rssUrl);
  http.addHeader("Accept-Charset", "utf-8");

  Serial.printf("[News] RSS 取得中: %s\n", rssUrl);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String xml = http.getString();
    Serial.printf("[News] RSS 取得完了 (%d bytes)\n", xml.length());

    if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
      extractItemsFromRss(xml);
      xSemaphoreGive(newsMutex);
      Serial.printf("[News] 見出し %d 件取得\n", (int)newsCount);
    } else {
      Serial.println(F("[News] Mutex 取得失敗、今回はスキップ"));
    }
  } else {
    Serial.printf("[News] HTTP 失敗 code=%d\n", httpCode);
  }

  http.end();
}

// ================================================================
// FreeRTOS タスク: Core 0 で RSS を 5 分ごとに取得
// ================================================================
static void newsFetchTask(void*)
{
  fetchNewsImpl();
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5UL * 60UL * 1000UL));
    fetchNewsImpl();
  }
}

// ================================================================
// [内部] 静止エリア: 見出し（タイトル）を折り返し多行描画
// ================================================================
static void drawNewsBody(const char *text)
{
  tft.fillRect(0, NEWS_STATIC_Y, SCREEN_W, NEWS_STATIC_H, COL_BG_NEWS);

  u8g2.setFontMode(1);
  u8g2.setFont(u8g2_font_b12_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setBackgroundColor(COL_BG_NEWS);

  const int lineH    = 16;
  const int maxLines = NEWS_STATIC_H / lineH;
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
// [内部] 電光掲示板エリア: 横スクロール描画
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
// [内部] タイトルバー描画 ("ニュース N/M  HH:MM")
// ================================================================
static void drawNewsTitleBar()
{
  tft.fillRect(0, NEWS_TITLE_Y, SCREEN_W, NEWS_TITLE_H, COL_BG_NEWSBAR);
  u8g2.setFontMode(1);
  u8g2.setBackgroundColor(COL_BG_NEWSBAR);

  const int baseY = NEWS_TITLE_Y + 14;

  // 左: "ニュース" ラベル
  u8g2.setFont(u8g2_font_b12_t_japanese3);
  u8g2.setForegroundColor(ST77XX_YELLOW);
  int labelW = u8g2.getUTF8Width("ニュース");
  u8g2.setCursor(4, baseY);
  u8g2.print("ニュース");

  // "ニュース" の直後: N/M カウンター
  if (newsCount > 0) {
    char counter[12];
    snprintf(counter, sizeof(counter), "%d/%d", newsIndex + 1, (int)newsCount);
    u8g2.setFont(u8g2_font_helvB10_tf);
    u8g2.setForegroundColor(ST77XX_WHITE);
    u8g2.setCursor(4 + labelW + 2, baseY);
    u8g2.print(counter);
  }

  // 右寄せ: 時刻 HH:MM
  struct tm ti;
  if (getLocalTime(&ti, 0)) {
    char timeStr[12];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &ti);
    u8g2.setFont(u8g2_font_helvB10_tf);
    u8g2.setForegroundColor(ST77XX_GREEN);
    int tw = u8g2.getUTF8Width(timeStr);
    u8g2.setCursor(SCREEN_W - tw - 4, baseY);
    u8g2.print(timeStr);
  }

  tft.drawFastHLine(0, NEWS_LINE_Y, SCREEN_W, ST77XX_WHITE);
}

// ================================================================
// [内部] スクロール状態を初期化（mutex 保持状態で呼ぶこと）
// ================================================================
static void initScroll()
{
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  scrollTextW = u8g2.getUTF8Width(newsDescriptions[newsIndex]);
  strncpy(currentScrollText, newsDescriptions[newsIndex], NEWS_DESC_MAX - 1);
  currentScrollText[NEWS_DESC_MAX - 1] = '\0';
  lastScrollMs = millis();
}

// ================================================================
// [内部] ページ遷移時の画面更新（mutex 保持状態で呼ぶこと）
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
// ニュース画面を描画する（公開 API）
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
      waitingForNews   = false;
      scrollPauseEndMs = 0;
      if (newsIndex < 0)              newsIndex = newsCount - 1;
      if (newsIndex >= (int)newsCount) newsIndex = 0;

      scrollX = SCREEN_W;
      drawNewsBody(newsItems[newsIndex]);
      initScroll();
      drawScrollBody(currentScrollText);
    }
    xSemaphoreGive(newsMutex);
  }
}

// ================================================================
// スクロール更新（loop() からニュース画面表示中のみ毎回呼ぶ）
//
// [状態遷移]
//   待機中 (waitingForNews)   : データ到着まで待つ
//   スクロール進行中           : 毎フレーム scrollX を進める
//   ポーズ中 (scrollPauseEndMs): タイムアウト後に次ページへ
// ================================================================
void updateNewsAutoPaging()
{
  unsigned long now = millis();

  // データ到着待ち
  if (waitingForNews && newsCount > 0) {
    if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    waitingForNews   = false;
    scrollPauseEndMs = 0;
    newsIndex        = 0;
    drawNewsPage();
    xSemaphoreGive(newsMutex);
    return;
  }

  if (newsCount == 0) return;

  // ポーズ中 → タイムアウト後に次ページへ
  if (scrollPauseEndMs != 0) {
    if (now < scrollPauseEndMs) return;

    if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
      scrollPauseEndMs = now + 200;
      return;
    }
    scrollPauseEndMs = 0;
    newsIndex        = (newsIndex + 1) % newsCount;
    drawNewsPage();
    xSemaphoreGive(newsMutex);
    return;
  }

  // スクロール進行中
  if (now - lastScrollMs < (unsigned long)SCROLL_TICK_MS) return;
  lastScrollMs = now;

  scrollX -= SCROLL_SPEED_PX;

  if (scrollX < -scrollTextW) {
    tft.fillRect(0, NEWS_TICKER_Y, SCREEN_W, NEWS_TICKER_H, COL_BG_NEWS);
    scrollPauseEndMs = now + TRANSITION_PAUSE_MS;
    return;
  }

  drawScrollBody(currentScrollText);
}

// ================================================================
// 次の見出しへ手動で進める（NEXT 短押し）
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
// ニュース画面表示中の時刻更新（loop() から 1 秒ごとに呼ぶ）
// ================================================================
void updateNewsClock()
{
  drawNewsTitleBar();
}

// ================================================================
// Web コンソール向けアクセサ
// ================================================================
int getNewsCount()
{
  return (int)newsCount;
}

bool getNewsItem(int i, char* titleBuf, int titleLen, char* descBuf, int descLen)
{
  if (i < 0 || i >= (int)newsCount) return false;
  if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(200)) != pdTRUE) return false;
  strncpy(titleBuf, newsItems[i],        titleLen - 1);
  titleBuf[titleLen - 1] = '\0';
  strncpy(descBuf,  newsDescriptions[i], descLen  - 1);
  descBuf[descLen  - 1] = '\0';
  xSemaphoreGive(newsMutex);
  return true;
}

// ================================================================
// ニュース機能の初期化（setup() で 1 回）
// ================================================================
void setupNews()
{
  newsMutex = xSemaphoreCreateMutex();
  if (newsMutex == nullptr) {
    Serial.println(F("[News] Mutex 作成失敗"));
    return;
  }

  newsCount            = 0;
  scrollX              = SCREEN_W;
  scrollTextW          = 0;
  currentScrollText[0] = '\0';
  waitingForNews       = false;
  lastScrollMs         = 0;
  scrollPauseEndMs     = 0;

  for (int i = 0; i < NEWS_MAX_ITEMS; i++) {
    newsItems[i][0]        = '\0';
    newsDescriptions[i][0] = '\0';
  }

  xTaskCreatePinnedToCore(
    newsFetchTask, "newsFetch",
    8192, nullptr, 1, nullptr, 0
  );

  Serial.println(F("[News] setupNews() 完了。Core 0 タスク起動済み。"));
}
