/**
 * ESP32 Weather Station - ニューステキストティッカー 実装
 *
 * [スクロール改善の仕組み]
 *
 * 旧方式の問題:
 *   fillRect(エリア全体) → 文字描画
 *   → ST7735S は SPI 転送なので「黒塗り → 文字」の間がちらつきの原因
 *
 * 新方式: 「前フレームの文字を上書き消し」方式
 *   1. 前フレームと今フレームの描画範囲を計算
 *   2. 今フレームのテキストを描画（背景色込みで上書き）
 *   3. 今フレームで文字が届かなかった右端の帯だけ fillRect で黒塗り
 *   → fillRect の幅が最小限になりちらつきが激減する
 *
 * さらに: フォントを b10 → b16 に変更（2倍近い大きさ）
 *   b16_t_japanese1 : 高さ16px → TICKER_AREA_H=20px に収まる
 */

#include "ticker.h"
#include "display.h"
#include <WiFi.h>
#include <HTTPClient.h>

// ================================================================
// 状態変数の実体定義
// ================================================================
String tickerText    = "ニュース取得中...";
int    tickerScrollX = 0;

// ================================================================
// 内部変数（このファイルのみ使用）
// ================================================================
static unsigned long lastTickerUpdate = 0;
static unsigned long lastNewsFetch    = 0;
static int  tickerTextPixelWidth      = 0;   // テキスト全体の描画幅（px）
static int  prevScrollX               = 0;   // 前フレームのスクロール位置

// ================================================================
// [内部ヘルパー] XML から <title> タグの内容を抽出する
//
// NHK RSS の構造:
//   <channel><title>NHKニュース</title>   ← 最初はチャンネル名（スキップ）
//   <item><title>見出し1</title></item>
//   <item><title>見出し2</title></item>
//
// ArduinoJson は XML 非対応なので、indexOf で文字列検索します。
// ================================================================
static String extractTitlesFromRss(const String &xml)
{
  String result     = "";
  int    searchFrom = 0;
  int    titleCount = 0;

  while (true)
  {
    int start = xml.indexOf("<title>", searchFrom);
    if (start == -1) break;
    start += 7;   // "<title>" の7文字を読み飛ばす

    int end = xml.indexOf("</title>", start);
    if (end == -1) break;

    if (titleCount > 0)   // 最初はチャンネル名なのでスキップ
    {
      String title = xml.substring(start, end);

      // XML エスケープ文字を復元
      title.replace("&amp;",  "&");
      title.replace("&lt;",   "<");
      title.replace("&gt;",   ">");
      title.replace("&quot;", "\"");

      if (result.length() > 0) result += "  ★  ";
      result += title;
    }

    titleCount++;
    searchFrom = end + 8;
  }

  return result;
}

// ================================================================
// NHK RSS を取得して tickerText を更新する
// ================================================================
void fetchNews()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(F("[Ticker] WiFi未接続のためスキップします。"));
    return;
  }

  const char *rssUrl = "https://www3.nhk.or.jp/rss/news/cat0.xml";
  HTTPClient http;
  http.setTimeout(5000);
  http.begin(rssUrl);

  Serial.println(F("[Ticker] NHK RSS取得中..."));
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK)
  {
    String xml = http.getString();
    Serial.printf("[Ticker] RSS取得完了 (%d bytes)\n", xml.length());

    String extracted = extractTitlesFromRss(xml);
    if (extracted.length() > 0)
    {
      tickerText    = extracted;
      tickerScrollX = 0;
      prevScrollX   = 0;
      Serial.printf("[Ticker] 見出し取得: %d文字\n", tickerText.length());
    }
    else
    {
      Serial.println(F("[Ticker] 見出し抽出失敗（XML構造確認が必要）"));
    }
  }
  else
  {
    Serial.printf("[Ticker] HTTP失敗 code=%d\n", httpCode);
  }

  http.end();
}

// ================================================================
// ティッカーの初期化（setup() から1回だけ呼ぶ）
// ================================================================
void setupTicker()
{
  fetchNews();

  lastNewsFetch    = millis();
  lastTickerUpdate = millis();

  // テキストの描画幅を計算（スクロール折り返し判定に使う）
  // b16_t_japanese1 は 16px 高の日本語フォント（旧 b10 より大きい）
  u8g2.setFont(u8g2_font_b16_t_japanese3);
   tickerTextPixelWidth = u8g2.getUTF8Width(tickerText.c_str());

   Serial.println(F("[Ticker] setupTicker() 完了。"));
}

// ================================================================
// ティッカーのスクロール描画・定期更新（loop() から毎回呼ぶ）
//
// [描画の最適化]
//   テキストを描画する際、u8g2 の背景色を TICKER_BG_COLOR（黒）に設定することで
//   文字の後ろが自動的に黒で塗りつぶされます。
//   これにより前フレームの文字残像が消え、fillRect の範囲を「右端の余白のみ」に
//   絞ることができます。
// ================================================================
void updateTicker()
{
  unsigned long now = millis();

  // ---- ① 5分ごとに RSS を再取得 ----
  if (now - lastNewsFetch >= TICKER_FETCH_INTERVAL)
  {
    fetchNews();
    lastNewsFetch = now;

   // テキスト幅を再計算
   u8g2.setFont(u8g2_font_b16_t_japanese3);
   tickerTextPixelWidth = u8g2.getUTF8Width(tickerText.c_str());
  }

  // ---- ② 更新間隔（20ms）待ち ----
  if (now - lastTickerUpdate < TICKER_UPDATE_INTERVAL) return;
  lastTickerUpdate = now;

  // ---- ③ テキストを描画 ----
  // X 座標を「画面右端 - スクロールオフセット」にすることで
  // テキストが右から左へ流れていきます。
  int drawX = TICKER_AREA_W - tickerScrollX;

  // u8g2 の背景色を黒にすることで文字の後ろが自動的に塗りつぶされます。
  // → 前フレームの文字が文字部分だけきれいに上書きされます。
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  u8g2.setForegroundColor(TICKER_TEXT_COLOR);
  u8g2.setBackgroundColor(TICKER_BG_COLOR);
  // ベースライン Y: エリア上端 + 16（フォント高さ分）
  u8g2.setCursor(drawX, TICKER_AREA_Y + 16);
  u8g2.print(tickerText.c_str());

  // ---- ④ テキストが届かない右端の余白を黒で塗りつぶす ----
  // テキストの右端座標を計算し、画面右端までが余白なら fillRect で黒塗り。
  // これにより前フレームのテキストの右側に残像が残りません。
  int textRight = drawX + tickerTextPixelWidth;
  if (textRight < TICKER_AREA_W)
  {
    // テキストが右端まで届かない場合のみ（スクロール開始直後など）
    tft.fillRect(textRight, TICKER_AREA_Y,
                 TICKER_AREA_W - textRight, TICKER_AREA_H,
                 TICKER_BG_COLOR);
  }

  // ---- ⑤ スクロール位置を進める ----
  prevScrollX   = tickerScrollX;
  tickerScrollX += TICKER_SCROLL_SPEED;

  // ---- ⑥ テキストが画面左端を完全に通過したらリセット ----
  // 「テキスト全体 + 画面幅分」スクロールしたら先頭に戻します。
  if (tickerScrollX > TICKER_AREA_W + tickerTextPixelWidth)
  {
    tickerScrollX = 0;
  }
}
