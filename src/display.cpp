/**
 * ESP32 Weather Station - 描画機能 実装
 *
 * [変更点]
 *   - drawClock() + drawCity() → drawClockCity() に統合（1行化）
 *   - drawTemp() + drawWeather() → drawCurrentWeather() に統合（コンパクト化）
 *   - 時計は HH:MM のみ（秒なし）で毎分更新
 *   - 気温＋天気詳細を AREA_WEATHER エリアに収める
 */

#include "display.h"
#include "cities.h"

// ================================================================
// 状態管理変数の実体定義（display.h で extern 宣言済み）
// ================================================================
int   cityIndex        = 0;      // 現在表示中の都市インデックス
float currentTemp      = 0;      // 現在の気温
int   currentWeatherCode = 0;    // 現在のWMO天気コード

// ================================================================
// 差分検出用キャッシュの実体
// ================================================================
char  prevTimeStr[16]  = "";     // 前回描画した時刻文字列（HH:MM）
int   prevCityIndex    = -1;     // -1 = 未描画（初回強制描画）
float prevTemp         = -999;   // -999 = ありえない値（初回強制描画）
int   prevWeatherCode  = -1;     // -1 = 未描画

// ================================================================
// タイマー管理変数の実体
// ================================================================
unsigned long lastFetchAttempt  = 0;
unsigned long lastWeeklyFetch   = 0;
unsigned long lastCitySwitch    = 0;
unsigned long lastClockUpdate   = 0;

// ================================================================
// 時計＋都市名エリアを描画（1行統合、差分描画対応）
//
// [レイアウト]
//   左端〜60px : HH:MM（緑、setTextSize(2) = 12×16px文字）
//   62px〜右端 : 都市名（黄色、日本語フォント b12）
//
// [差分描画の仕組み]
//   strftime で "HH:MM" の文字列を作り、前回描画した prevTimeStr と比較。
//   同じなら描画をスキップすることで、毎ループ呼んでも負荷がかかりません。
//   都市インデックスが変わった場合は時刻文字列が同じでも再描画します。
// ================================================================
void drawClockCity()
{
  struct tm ti;
  if (!getLocalTime(&ti)) return;   // NTP 同期が完了していなければスキップ

  // 秒なしの "HH:MM" 形式に変換
  char timeStr[16];
  strftime(timeStr, sizeof(timeStr), "%H:%M", &ti);

  // 前回と時刻も都市も同じなら描画スキップ（ちらつき防止）
  if (strcmp(timeStr, prevTimeStr) == 0 && prevCityIndex == cityIndex) return;

  // 更新があったのでキャッシュを記録
  strcpy(prevTimeStr, timeStr);

  // エリア全体を時計用背景色で塗りつぶす
  tft.fillRect(0, AREA_CLOCK_Y, SCREEN_W, AREA_CLOCK_H, COL_BG_CLOCK);

  // ---- 時刻（左端、緑色、setTextSize(2)）----
  // setTextSize(2) は内蔵フォントを 2倍（12×16px）に拡大します。
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(2, 7);   // 上下中央: (30 - 16) / 2 = 7
  tft.print(timeStr);

  // ---- 都市名（右側、黄色、日本語フォント）----
   u8g2.setFont(u8g2_font_b16_t_japanese3);
   u8g2.setForegroundColor(ST77XX_YELLOW);
   u8g2.setBackgroundColor(COL_BG_CLOCK);
   u8g2.setCursor(64, AREA_CLOCK_Y + AREA_CLOCK_H - 3);
  u8g2.print(cities[cityIndex].name);

  Serial.printf("[Clock] %s  %s\n", timeStr, cities[cityIndex].name);
}

// ================================================================
// 現在天気エリアを描画（アイコン＋気温＋天気詳細を1エリアに統合）
//
// [レイアウト（AREA_WEATHER_Y〜+44px）]
//   左端(x=2, y+2): 天気アイコン 36×36px
//   x=44, 上段   : 気温（大）+ °C（小）
//   x=44, 下段   : 天気詳細テキスト（日本語、小さめ）
//
// drawWeatherInfo() が背景を事前に塗りつぶしているので
// ここでは clearArea を呼ばずに直接描画します。
// ================================================================
void drawCurrentWeather()
{
  // ---- 天気アイコン（左端、36×36px）----
  drawWeatherIcon(2, AREA_WEATHER_Y + 4, currentWeatherCode);

  // ---- 気温（上段右側）----
  // logisoso20_tr : 数字専用の細身フォント（高さ約20px）
  u8g2.setFont(u8g2_font_logisoso20_tr);
  u8g2.setForegroundColor(ST77XX_CYAN);
  // ベースライン Y = エリア上端 + 24（上段の中央あたり）
  u8g2.setCursor(44, AREA_WEATHER_Y + 24);
  char buf[8];
  snprintf(buf, sizeof(buf), "%.1f", currentTemp);
  u8g2.print(buf);

  // °C を小さいフォントで続けて描画
  u8g2.setFont(u8g2_font_logisoso20_tr);
  u8g2.print("°C");

 // ---- 天気詳細テキスト（下段、日本語）----
   u8g2.setFont(u8g2_font_b16_t_japanese3);
   u8g2.setForegroundColor(ST77XX_WHITE);
   u8g2.setCursor(44, AREA_WEATHER_Y + 38);
  String ws = getWeatherJp(currentWeatherCode);
  u8g2.print(ws.c_str());
}

// ================================================================
// 天気情報をまとめて更新（都市切替・API更新時に呼ぶ）
//
// [処理の流れ]
//   1. 天気コードに応じた背景色を取得
//   2. 時計エリア下〜画面下端（ティッカーエリア除く）を背景色で一括塗りつぶし
//   3. u8g2 の背景色を画面と合わせる（黒い矩形が文字の後ろに残るのを防ぐ）
//   4. 時計＋都市名、現在天気、週間天気を描画
//   5. キャッシュを更新（次回差分チェック用）
// ================================================================
void drawWeatherInfo()
{
  uint16_t bg = getBgColor(currentWeatherCode);

  // 時計エリアの下からティッカーエリアの手前（Y=139）まで一括塗りつぶし
  // （ティッカーエリアは ticker.cpp が管理するため触らない）
  tft.fillRect(0, AREA_CLOCK_Y, SCREEN_W, AREA_TICKER_Y - AREA_CLOCK_Y, bg);

  // u8g2 の背景色を画面と合わせる
  u8g2.setBackgroundColor(bg);

  // 各エリアを描画
  drawClockCity();
  drawCurrentWeather();
  drawWeeklyForecast();   // 週間天気（weather.cpp で実装）

  // キャッシュ更新
  prevTemp        = currentTemp;
  prevWeatherCode = currentWeatherCode;
  prevCityIndex   = cityIndex;

  Serial.printf("[Display] %s %.1f°C code=%d\n",
                cities[cityIndex].name, currentTemp, currentWeatherCode);
}

// ================================================================
// 起動時に一度だけ呼ぶ静的要素の描画
// ================================================================
void drawStaticElements()
{
  tft.fillScreen(ST77XX_BLACK);

  // 時計エリアと現在天気エリアの区切り線（白）
  tft.drawFastHLine(0, LINE_Y1, SCREEN_W, ST77XX_WHITE);

  // 現在天気エリアと週間天気エリアの区切り線（薄いグレー）
  tft.drawFastHLine(0, LINE_Y2, SCREEN_W, 0x4208);

  // 週間天気エリアとティッカーエリアの区切り線（白）
  tft.drawFastHLine(0, LINE_Y3, SCREEN_W, ST77XX_WHITE);
}
