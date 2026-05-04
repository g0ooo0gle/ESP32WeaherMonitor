/**
 * ESP32 Weather Station - ニュース画面 実装
 *
 * [レイアウト 案A: 見出し + description を1ページに表示]
 *   Y=  0〜19 : タイトルバー "ニュース 3/12"
 *   Y= 20     : 白区切り線
 *   Y= 22〜?  : 見出し (シアン, 最大2行)
 *   Y= ?+2    : グレー区切り線
 *   Y= ?+5〜  : description (白, 最大6行)
 *
 * [RSS 解析]
 *   <item> ブロック単位で title と description を取得。
 *   description は CDATA / HTML タグ / XMLエンティティを cleanXmlText() でクリーニング。
 *
 * [タスク間データ共有]
 *   newsItems[] / newsDescs[] : Mutex (newsMutex) で保護
 */

#include "ticker.h"
#include "display.h"
#include "button.h"
#include <WiFi.h>
#include <HTTPClient.h>

// ================================================================
// 共有データ
// ================================================================
static char newsItems[NEWS_MAX_ITEMS][NEWS_TITLE_MAX];
static char newsDescs[NEWS_MAX_ITEMS][NEWS_DESC_MAX];
static int  newsCount = 0;

static SemaphoreHandle_t newsMutex = nullptr;
static int               newsIndex = 0;
static unsigned long     lastNewsPageChange = 0;

// ================================================================
// UTF-8 として不正なバイト列を '?' に置換
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
// XML テキストのクリーニング
//   1. CDATA ラッパーを除去
//   2. HTML タグ (<...>) を除去
//   3. XML エンティティを展開
//   4. 改行をスペースに変換して trim
// ================================================================
static void cleanXmlText(String &s)
{
  // CDATA アンラップ
  int cdStart = s.indexOf("<![CDATA[");
  if (cdStart >= 0) {
    int cdEnd = s.indexOf("]]>", cdStart);
    s = (cdEnd >= 0) ? s.substring(cdStart + 9, cdEnd)
                     : s.substring(cdStart + 9);
  }

  // HTML タグ除去
  String result;
  result.reserve(s.length());
  bool inTag = false;
  for (int i = 0; i < (int)s.length(); i++) {
    char c = s[i];
    if      (c == '<') { inTag = true;  continue; }
    else if (c == '>') { inTag = false; continue; }
    else if (!inTag)   result += c;
  }
  s = result;

  // XML エンティティ展開
  s.replace("&amp;",  "&");
  s.replace("&lt;",   "<");
  s.replace("&gt;",   ">");
  s.replace("&quot;", "\"");
  s.replace("&apos;", "'");
  s.replace("&nbsp;", " ");
  s.replace("&#13;",  "");
  s.replace("&#10;",  " ");
  s.replace("\r",     "");
  s.replace("\n",     " ");

  s.trim();
}

// ================================================================
// <item> ブロックから title と description を抽出
// ================================================================
static void extractItemsFromRss(const String &xml)
{
  newsCount = 0;
  int searchFrom = 0;

  while (newsCount < NEWS_MAX_ITEMS)
  {
    int itemStart = xml.indexOf("<item>", searchFrom);
    if (itemStart == -1) break;
    int itemEnd = xml.indexOf("</item>", itemStart);
    if (itemEnd == -1) break;

    String item = xml.substring(itemStart + 6, itemEnd);

    int ts = item.indexOf("<title>");
    int te = item.indexOf("</title>");
    int ds = item.indexOf("<description>");
    int de = item.indexOf("</description>");

    if (ts != -1 && te != -1 && ts < te) {
      String title = item.substring(ts + 7, te);
      cleanXmlText(title);

      if (title.length() > 0) {
        String desc = "";
        if (ds != -1 && de != -1 && ds < de) {
          desc = item.substring(ds + 13, de);
          cleanXmlText(desc);
        }

        strncpy(newsItems[newsCount], title.c_str(), NEWS_TITLE_MAX - 1);
        newsItems[newsCount][NEWS_TITLE_MAX - 1] = '\0';
        sanitizeUtf8(newsItems[newsCount]);

        strncpy(newsDescs[newsCount], desc.c_str(), NEWS_DESC_MAX - 1);
        newsDescs[newsCount][NEWS_DESC_MAX - 1] = '\0';
        sanitizeUtf8(newsDescs[newsCount]);

        newsCount++;
      }
    }

    searchFrom = itemEnd + 7;
  }
}

// ================================================================
// NHK RSS 取得
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
      extractItemsFromRss(xml);
      xSemaphoreGive(newsMutex);
      Serial.printf("[News] %d件 取得\n", newsCount);
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
// FreeRTOS タスク: Core 0 で5分ごとに RSS 取得
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
// テキストを自動改行しながら描画し、最終行の下端Y座標を返す
//
// startY  : 第1行の上端Y
// maxLines: 最大描画行数
// color   : 文字色 (RGB565)
// returns : startY + drawnLines * lineH
// ================================================================
static int drawWrappedText(const char *text, int startY, int maxLines, uint16_t color)
{
  const int lineH   = 16;   // フォント12px + 行間4px
  const int leftPad = 4;
  const int bodyW   = SCREEN_W - leftPad * 2;

  u8g2.setFontMode(1);
  u8g2.setFont(u8g2_font_b12_t_japanese3);
  u8g2.setForegroundColor(color);
  u8g2.setBackgroundColor(COL_BG_NEWS);

  char lineBuf[128];
  int  lineLen   = 0;
  int  lineCount = 0;
  int  i         = 0;

  while (text[i] != '\0' && lineCount < maxLines)
  {
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
      // 幅オーバー → 現在の行を確定して次の行へ
      lineBuf[lineLen] = '\0';
      u8g2.setCursor(leftPad, startY + lineH * (lineCount + 1) - 2);
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

  // 末尾の残り
  if (lineLen > 0 && lineCount < maxLines) {
    lineBuf[lineLen] = '\0';
    u8g2.setCursor(leftPad, startY + lineH * (lineCount + 1) - 2);
    u8g2.print(lineBuf);
    lineCount++;
  }

  return startY + lineCount * lineH;
}

// ================================================================
// 本文エリア描画: 見出し(シアン) + 区切り線 + description(白)
// ================================================================
static void drawNewsBody(const char *title, const char *desc)
{
  tft.fillRect(0, NEWS_BODY_Y, SCREEN_W, NEWS_BODY_H, COL_BG_NEWS);

  // 見出し (最大2行)
  int sepY = drawWrappedText(title, NEWS_BODY_Y, 2, ST77XX_CYAN);

  // グレー区切り線
  sepY += 2;
  tft.drawFastHLine(0, sepY, SCREEN_W, 0x4208);
  int descY = sepY + 3;

  // description
  if (desc && desc[0] != '\0') {
    int maxLines = (SCREEN_H - descY) / 16;
    if (maxLines > 6) maxLines = 6;
    drawWrappedText(desc, descY, maxLines, ST77XX_WHITE);
  }
}

// ================================================================
// タイトルバー描画 ("ニュース 3/12")
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
// ニュース画面描画 (公開 API)
// ================================================================
void drawNewsScreen()
{
  tft.fillScreen(COL_BG_NEWS);
  drawNewsTitleBar();

  if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    if (newsCount == 0) {
      u8g2.setFontMode(1);
      u8g2.setFont(u8g2_font_b12_t_japanese3);
      u8g2.setForegroundColor(ST77XX_WHITE);
      u8g2.setBackgroundColor(COL_BG_NEWS);
      u8g2.setCursor(8, NEWS_BODY_Y + 30);
      u8g2.print("ニュース取得中...");
    } else {
      if (newsIndex < 0)          newsIndex = newsCount - 1;
      if (newsIndex >= newsCount) newsIndex = 0;

      drawNewsBody(newsItems[newsIndex], newsDescs[newsIndex]);
    }
    xSemaphoreGive(newsMutex);
  }

  lastNewsPageChange = millis();
}

// ================================================================
// 自動ページ切替 (loop() からニュース画面表示中のみ呼ぶ)
// ================================================================
void updateNewsAutoPaging()
{
  if (millis() - lastNewsPageChange < newsPageInterval) return;
  if (newsCount > 0) newsIndex = (newsIndex + 1) % newsCount;
  drawNewsScreen();
}

// ================================================================
// 次の見出しへ
// ================================================================
void nextNewsPage()
{
  if (newsCount == 0) return;
  newsIndex = (newsIndex + 1) % newsCount;
  drawNewsScreen();
}

// ================================================================
// 前の見出しへ
// ================================================================
void prevNewsPage()
{
  if (newsCount == 0) return;
  newsIndex = (newsIndex - 1 + newsCount) % newsCount;
  drawNewsScreen();
}

// ================================================================
// 初期化 (setup() で1回だけ呼ぶ)
// ================================================================
void setupNews()
{
  newsMutex = xSemaphoreCreateMutex();
  if (newsMutex == nullptr) {
    Serial.println(F("[News] Mutex作成失敗"));
    return;
  }

  newsCount = 0;
  for (int i = 0; i < NEWS_MAX_ITEMS; i++) {
    newsItems[i][0] = '\0';
    newsDescs[i][0] = '\0';
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
