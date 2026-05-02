/**
 * ESP32 Weather Station - ニューステキストティッカー 実装
 *
 * [処理の流れ]
 *   1. fetchNews()  : HTTPでNHKのRSSを取得 → <title>タグを抽出 → tickerText に格納
 *   2. updateTicker(): 40ms ごとに tickerScrollX を 2px ずつ増やして画面に描画
 *      → テキスト末尾を超えたら scrollX = 0 に戻してループ
 *      → 5分ごとに fetchNews() を自動呼び出してニュースを更新
 *
 * [差分描画でちらつきを防ぐ工夫]
 *   毎回エリア全体を塗りつぶしてから文字を書くと、塗り→描画の一瞬にちらつきます。
 *   対策: スクロール前のテキスト位置をクリップ領域（setClipWindow）で
 *   塗りつぶす代わりに、背景色で文字の後ろだけを消します。
 *   ただし ST7735 の Adafruit_GFX にはクリップ機能がないため、
 *   ここでは「背景色で矩形塗りつぶし→その上に文字描画」という
 *   シンプルな方法を採用します（低速描画は問題になりません）。
 *
 * [NHKニュースRSSについて]
 * URL: https://www3.nhk.or.jp/rss/news/cat0.xml
 * cat0 = 主要ニュース。他カテゴリ: cat1=社会, cat2=科学, cat3=政治, cat4=経済
 * XMLの構造: <item><title>見出し</title>... が複数並んでいます。
 */

#include "ticker.h"
#include "display.h"   // tft, u8g2 インスタンスを使うため
#include <WiFi.h>
#include <HTTPClient.h>

// ================================================================
// 状態変数の実体定義
// ================================================================
String tickerText   = "ニュース取得中...";   // 現在表示中のテキスト（初期値）
int    tickerScrollX = 0;                    // 現在のスクロールオフセット（px）

// ================================================================
// 内部（このファイルだけで使う）変数
// static を付けると他ファイルからアクセスできなくなります（情報隠蔽）。
// ================================================================
static unsigned long lastTickerUpdate = 0;   // 最後にスクロール描画した時刻
static unsigned long lastNewsFetch    = 0;   // 最後にRSS取得した時刻
static int tickerTextPixelWidth       = 0;   // tickerText を描画したときの横幅(px)
                                             // スクロールの折り返し判定に使います

// ================================================================
// [内部ヘルパー] XML文字列から <title> タグの中身を全て抽出する
//
// NHKのRSSはざっくりこんな形になっています:
//   <channel>
//     <title>NHKニュース</title>      ← チャンネル名（不要）
//     <item>
//       <title>見出し1</title>        ← ここが欲しい
//     </item>
//     <item>
//       <title>見出し2</title>
//     </item>
//   </channel>
//
// ArduinoJson は XML を扱えないので、文字列検索で地道に抽出します。
// 最初の <title> はチャンネル名なので、2番目以降を取得します。
//
// 引数 xml: HTTPレスポンス全体の文字列
// 戻り値  : "見出し1  ★  見出し2  ★  ..." という1本の長い文字列
// ================================================================
static String extractTitlesFromRss(const String &xml)
{
  String result = "";       // 結果を格納する文字列
  int searchFrom = 0;       // 検索開始位置（前回見つけた場所の続きから）
  int titleCount = 0;       // 何番目の <title> か（最初はチャンネル名なのでスキップ）

  while (true)
  {
    // "<title>" の次の文字位置を探す
    int start = xml.indexOf("<title>", searchFrom);
    if (start == -1) break;   // もう <title> がなければ終了
    start += 7;               // "<title>" の7文字分を読み飛ばして中身の先頭へ

    // "</title>" の位置を探す
    int end = xml.indexOf("</title>", start);
    if (end == -1) break;     // タグが閉じていなければ終了（壊れたXML対策）

    // 最初の <title> はチャンネル名（"NHKニュース" など）なのでスキップ
    if (titleCount > 0)
    {
      String title = xml.substring(start, end);   // タグ間のテキストを切り出す

      // XMLエスケープ文字を元に戻す（最低限の変換）
      // &amp; → & 、&lt; → < 、&gt; → > 、&quot; → "
      title.replace("&amp;",  "&");
      title.replace("&lt;",   "<");
      title.replace("&gt;",   ">");
      title.replace("&quot;", "\"");

      // 2番目以降の見出しは区切り文字を挟んで連結
      if (result.length() > 0) result += "  ★  ";
      result += title;
    }

    titleCount++;
    searchFrom = end + 8;   // "</title>" の8文字分を読み飛ばして次の検索へ
  }

  return result;
}

// ================================================================
// NHKニュースのRSSを取得してtickerTextを更新する
// WiFiが未接続の場合はスキップします。
// ================================================================
void fetchNews()
{
  // WiFiが接続されていなければ取得できないのでスキップ
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(F("[Ticker] WiFi未接続のためニュース取得をスキップします。"));
    return;
  }

  // NHKニュース主要ニュースのRSSフィードURL
  // cat0 = 主要ニュース。变えたい場合は cat1〜cat7 を試してください。
  const char *rssUrl = "https://www3.nhk.or.jp/rss/news/cat0.xml";

  HTTPClient http;
  http.setTimeout(5000);   // 5秒でタイムアウト（RSSはやや重めなので天気より長め）
  http.begin(rssUrl);

  Serial.println(F("[Ticker] NHK RSSを取得中..."));
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK)
  {
    // レスポンスボディ（XML文字列）を取得
    // getString() はメモリを消費するため、ESP32の十分なヒープがある場合のみ有効
    String xml = http.getString();
    Serial.printf("[Ticker] RSS取得完了 (%d bytes)\n", xml.length());

    // XMLから見出しを抽出して tickerText に格納
    String extracted = extractTitlesFromRss(xml);

    if (extracted.length() > 0)
    {
      tickerText    = extracted;
      tickerScrollX = 0;   // 新しいテキストが来たのでスクロールを先頭に戻す
      Serial.printf("[Ticker] 見出し取得成功（%d文字）\n", tickerText.length());
    }
    else
    {
      // 抽出に失敗した場合（XMLの構造が変わった・空の場合など）
      Serial.println(F("[Ticker] 見出しの抽出に失敗しました（XML構造を確認してください）。"));
    }
  }
  else
  {
    Serial.printf("[Ticker] RSS取得失敗 HTTP code: %d\n", httpCode);
  }

  http.end();   // 接続を閉じてメモリを解放
}

// ================================================================
// ティッカーの初期化
// setup() から1回だけ呼んでください。
// ================================================================
void setupTicker()
{
  // 最初のニュースをすぐ取得する
  fetchNews();

  // タイマーを現在時刻で初期化
  lastNewsFetch    = millis();
  lastTickerUpdate = millis();

  // tickerText の描画ピクセル幅を計算しておく（スクロール折り返し判定用）
  // u8g2 の getUTF8Width() で日本語を含む文字列の幅を正確に取得できます。
  u8g2.setFont(u8g2_font_b10_t_japanese1);   // ティッカー用の小さめ日本語フォント
  tickerTextPixelWidth = u8g2.getUTF8Width(tickerText.c_str());

  Serial.println(F("[Ticker] setupTicker() 完了。"));
}

// ================================================================
// ティッカーのスクロール描画・定期更新
// loop() から毎回呼んでください。
// 内部でタイマーを管理しているので、呼びすぎても問題ありません。
// ================================================================
void updateTicker()
{
  unsigned long now = millis();

  // ---- ① 定期ニュース更新チェック（5分ごと）----
  // TICKER_FETCH_INTERVAL (5分) が経過していたら RSS を再取得します。
  if (now - lastNewsFetch >= TICKER_FETCH_INTERVAL)
  {
    fetchNews();
    lastNewsFetch = now;

    // テキストが変わった可能性があるので幅を再計算
    u8g2.setFont(u8g2_font_b10_t_japanese1);
    tickerTextPixelWidth = u8g2.getUTF8Width(tickerText.c_str());
  }

  // ---- ② スクロール描画チェック（40msごと）----
  // TICKER_UPDATE_INTERVAL (40ms) が経過していなければ何もしない。
  if (now - lastTickerUpdate < TICKER_UPDATE_INTERVAL) return;
  lastTickerUpdate = now;

  // ---- ③ ティッカーエリアを背景色で塗りつぶし（前フレームの文字を消す）----
  // tft.fillRect(x, y, 幅, 高さ, 色) でティッカーエリア全体を黒で塗りつぶします。
  tft.fillRect(0, TICKER_AREA_Y, TICKER_AREA_W, TICKER_AREA_H, TICKER_BG_COLOR);

  // ---- ④ テキストを現在のスクロール位置で描画 ----
  // tickerScrollX px だけ左にずらした位置から描画します。
  // setCursor のX座標を負にすることで、テキストが画面左からはみ出て見えます。
  u8g2.setFont(u8g2_font_b10_t_japanese1);   // 小さめの日本語フォント（10px高）
  u8g2.setForegroundColor(TICKER_TEXT_COLOR);
  u8g2.setBackgroundColor(TICKER_BG_COLOR);
  u8g2.setCursor(TICKER_AREA_W - tickerScrollX, TICKER_AREA_Y + 14);
                 // ↑ 「画面右端から始めて左に動かす」ことで、
                 //    最初はテキストが右から流れてくるように見えます。
  u8g2.print(tickerText.c_str());

  // ---- ⑤ スクロール位置を進める ----
  tickerScrollX += TICKER_SCROLL_SPEED;

  // ---- ⑥ テキストが画面の左端を完全に通過したらリセット ----
  // tickerScrollX が「画面幅 + テキストの幅」を超えたら先頭に戻します。
  // これにより、最後の文字が画面左端から消えた直後に
  // 先頭の文字が右端から再び流れてきます（シームレスなループ）。
  if (tickerScrollX > TICKER_AREA_W + tickerTextPixelWidth)
  {
    tickerScrollX = 0;
  }
}
