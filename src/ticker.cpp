/**
 * ESP32 Weather Station - ニュース画面 実装
 *
 * [このファイルでやること]
 *   1. NHK RSS を Core 0 のタスクで非同期取得
 *   2. 見出しを配列に貯める (Mutex 保護)
 *   3. ニュース画面で 1見出しを全画面ページ表示
 *   4. UTF-8 文字単位で自動改行
 *   5. 5秒で次の見出しへ自動切替 / ボタンで手動切替
 *
 * [自動改行の仕組み]
 *   ST7735 (128px幅) では b12_t_japanese3 で日本語1文字≒12px、
 *   1行にだいたい 9-10 文字。表示幅が NEWS_BODY_W を超えたら改行する
 *   方式で、UTF-8 のシーケンス境界を尊重して切ります。
 *
 *   u8g2.getUTF8Width() を使うと特定の文字列の描画幅が分かるので、
 *   1文字ずつ追加しながら幅を測り、超えたら改行する単純な方式です。
 *
 * [タスク間データ共有]
 *   newsItems[]      : 取得済みの見出し配列 (共有)
 *   newsCount        : 取得済み件数 (共有)
 *   newsMutex        : 上記の保護
 */

#include "ticker.h"
#include "display.h"
#include "button.h"   // currentScreen 判定に使う
#include <WiFi.h>
#include <HTTPClient.h>

// ================================================================
// 共有データ (タスクとメインの両方からアクセス)
// ================================================================
static char newsItems[NEWS_MAX_ITEMS][NEWS_TITLE_MAX];
static int  newsCount = 0;

static SemaphoreHandle_t newsMutex = nullptr;

// 現在表示中の見出しインデックス
static int  newsIndex = 0;

// 自動切替用タイマー
static unsigned long lastNewsPageChange = 0;

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
  // 一時的にカウントを 0 にして書き直す
  // (Mutex 保護はこの関数を呼ぶ側で行う)
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

    if (titleCount > 0)   // 0件目はチャンネル名なのでスキップ
    {
      String title = xml.substring(start, end);
      // XML エスケープ復元
      title.replace("&amp;",  "&");
      title.replace("&lt;",   "<");
      title.replace("&gt;",   ">");
      title.replace("&quot;", "\"");
      title.replace("&apos;", "'");
      // CDATA タグが残っていれば取り除く
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
// FreeRTOS タスク: Core 0 で動作する RSS 取得ループ
// 5分ごとに RSS を再取得します。
// ================================================================
static void newsFetchTask(void * /*param*/)
{
  fetchNewsImpl();   // 起動直後に1回

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5UL * 60UL * 1000UL));   // 5分待機
    fetchNewsImpl();
  }
}

// ================================================================
// [内部] 1見出しを自動改行しながら描画
//
// アルゴリズム:
//   1. 文字列を1文字 (UTF-8) ずつ「行バッファ」に追加
//   2. 追加するごとに u8g2.getUTF8Width() で幅を測る
//   3. 幅が NEWS_BODY_W を超えたら改行 (行バッファをリセット)
//   4. 行が画面の高さを超えそうなら描画を止める
//
// UTF-8 1文字の長さ:
//   - 0xxxxxxx -> 1byte (ASCII)
//   - 110xxxxx -> 2byte
//   - 1110xxxx -> 3byte (日本語の多くがここ)
//   - 11110xxx -> 4byte
// ================================================================
static void drawNewsBody(const char *text)
{
  // 本文エリア塗りつぶし
  tft.fillRect(0, NEWS_BODY_Y, SCREEN_W, NEWS_BODY_H, COL_BG_NEWS);

  u8g2.setFontMode(1);
  u8g2.setFont(u8g2_font_b12_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setBackgroundColor(COL_BG_NEWS);

  // 1行の高さ (ピクセル) と本文エリアに収まる行数
  const int lineH    = 16;                            // フォント12px + 行間4px
  const int maxLines = NEWS_BODY_H / lineH;           // 138/16 = 8行
  const int leftPad  = 4;                             // 左マージン
  const int bodyW    = SCREEN_W - leftPad * 2;        // 描画可能幅

  // 行バッファ (UTF-8で扱える長さで十分に確保)
  char lineBuf[NEWS_TITLE_MAX];
  int  lineLen   = 0;
  int  lineCount = 0;

  int i = 0;
  while (text[i] != '\0' && lineCount < maxLines)
  {
    // 1文字 (UTF-8) を取り出して seqLen バイト分を取得
    unsigned char c = (unsigned char)text[i];
    int seqLen = 1;
    if      ((c & 0xE0) == 0xC0) seqLen = 2;
    else if ((c & 0xF0) == 0xE0) seqLen = 3;
    else if ((c & 0xF8) == 0xF0) seqLen = 4;

    // 行バッファに仮追加して幅を測る
    if (lineLen + seqLen >= (int)sizeof(lineBuf) - 1) break;
    for (int k = 0; k < seqLen; k++) lineBuf[lineLen + k] = text[i + k];
    lineBuf[lineLen + seqLen] = '\0';

    int w = u8g2.getUTF8Width(lineBuf);

    if (w > bodyW) {
      // 幅オーバー → 仮追加した文字を取り消し、現在の行を確定
      lineBuf[lineLen] = '\0';

      // 描画
      u8g2.setCursor(leftPad, NEWS_BODY_Y + lineH * (lineCount + 1) - 2);
      u8g2.print(lineBuf);
      lineCount++;

      // 次の行を新しい文字から開始 (取り消した文字を再追加)
      lineLen = 0;
      if (lineCount < maxLines) {
        for (int k = 0; k < seqLen; k++) lineBuf[lineLen + k] = text[i + k];
        lineLen += seqLen;
        lineBuf[lineLen] = '\0';
      }
    } else {
      // 幅OK → 確定追加
      lineLen += seqLen;
    }

    i += seqLen;
  }

  // 最後に残った行を描画
  if (lineLen > 0 && lineCount < maxLines) {
    lineBuf[lineLen] = '\0';
    u8g2.setCursor(leftPad, NEWS_BODY_Y + lineH * (lineCount + 1) - 2);
    u8g2.print(lineBuf);
  }
}

// ================================================================
// [内部] タイトルバーを描画 ("ニュース 3/12")
// ================================================================
static void drawNewsTitleBar()
{
  // バー塗りつぶし
  tft.fillRect(0, NEWS_TITLE_Y, SCREEN_W, NEWS_TITLE_H, COL_BG_NEWSBAR);

  u8g2.setFontMode(1);
  u8g2.setFont(u8g2_font_b12_t_japanese3);
  u8g2.setForegroundColor(ST77XX_YELLOW);
  u8g2.setCursor(4, NEWS_TITLE_Y + 14);
  u8g2.print("ニュース");

  // 右側に "3/12" のような件数表示
  if (newsCount > 0) {
    char counter[16];
    snprintf(counter, sizeof(counter), "%d/%d", newsIndex + 1, newsCount);
    u8g2.setFont(u8g2_font_helvB10_tf);
    u8g2.setForegroundColor(ST77XX_WHITE);
    // 右寄せのため幅を測ってから x を決める
    int cw = u8g2.getUTF8Width(counter);
    u8g2.setCursor(SCREEN_W - cw - 4, NEWS_TITLE_Y + 14);
    u8g2.print(counter);
  }

  // バー下の区切り線
  tft.drawFastHLine(0, NEWS_LINE_Y, SCREEN_W, ST77XX_WHITE);
}

// ================================================================
// ニュース画面を描画する (公開API)
// ================================================================
void drawNewsScreen()
{
  // 背景全体 (本文エリア用の暗い紫)
  tft.fillScreen(COL_BG_NEWS);

  // タイトルバー
  drawNewsTitleBar();

  // 本文 (Mutex 保護で newsItems[] を読む)
  if (xSemaphoreTake(newsMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    if (newsCount == 0) {
      // データ未取得のメッセージ
      u8g2.setFontMode(1);
      u8g2.setFont(u8g2_font_b12_t_japanese3);
      u8g2.setForegroundColor(ST77XX_WHITE);
      u8g2.setBackgroundColor(COL_BG_NEWS);
      u8g2.setCursor(8, NEWS_BODY_Y + 30);
      u8g2.print("ニュース取得中...");
    } else {
      // インデックス範囲内に補正
      if (newsIndex < 0)             newsIndex = newsCount - 1;
      if (newsIndex >= newsCount)    newsIndex = 0;

      // 本文描画 (ローカルにコピーしてから Mutex を解放するのが
      // より安全だが、描画は速いのでこのまま保持で進めます)
      drawNewsBody(newsItems[newsIndex]);
    }
    xSemaphoreGive(newsMutex);
  }

  lastNewsPageChange = millis();
}

// ================================================================
// 自動ページ切替 (ニュース画面表示中のみ呼ぶ)
// ================================================================
void updateNewsAutoPaging()
{
  unsigned long now = millis();
  if (now - lastNewsPageChange < newsPageInterval) return;

  // 5秒経過 → 次の見出しへ
  if (newsCount > 0) {
    newsIndex = (newsIndex + 1) % newsCount;
    drawNewsScreen();
  } else {
    // 取得待ちの間も画面更新 (取得完了したら見出しが出る)
    drawNewsScreen();
  }
}

// ================================================================
// 次の見出しへ手動で進める (CITY短押し)
// ================================================================
void nextNewsPage()
{
  if (newsCount == 0) return;
  newsIndex = (newsIndex + 1) % newsCount;
  drawNewsScreen();
}

// ================================================================
// 前の見出しへ戻す (CITY長押し)
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

  // 全バッファを 0 クリア
  newsCount = 0;
  for (int i = 0; i < NEWS_MAX_ITEMS; i++) {
    newsItems[i][0] = '\0';
  }

  // RSS 取得タスクを Core 0 で起動
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
