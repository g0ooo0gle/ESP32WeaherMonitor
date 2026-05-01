/**
 * ESP32 Weather Station - 描画機能 実装
 */

#include "display.h"
#include "cities.h"

// ================================================================
// 状態管理変数の実体定義
// main.cpp でも extern 宣言して使用しています。
// ここが唯一の「定義場所」です。
// ================================================================
int cityIndex = 0;                          // 現在表示中の都市インデックス（最初は0番目）
float currentTemp = 0;                      // 現在の気温（初期値0）
int currentWeatherCode = 0;                 // 現在のWMO天気コード（初期値0）

// ================================================================
// 差分検出用キャッシュの実体
// 値が -1 や -999 の場合は「未描画」として扱います（初回描画を強制）。
// ================================================================
char prevTimeStr[16] = "";                  // 前回描画した時刻文字列
int prevCityIndex = -1;                     // 前回の都市インデックス（初回は必ず描画）
float prevTemp = -999;                      // 前回の気温（-999はありえない値）
int prevWeatherCode = -1;                   // 前回の天気コード（初回は必ず描画）

// ================================================================
// タイマー管理変数の実体
// setup() で初期化されます。
// ================================================================
unsigned long lastFetchAttempt = 0;         // API通信の前回試行時刻
unsigned long lastCitySwitch = 0;           // 都市切替の前回実行時刻
unsigned long lastClockUpdate = 0;          // 時計描画の前回実行時刻

// ================================================================
// 時計エリアを描画（差分描画対応済み）
// NTPから取得した現在の時刻を描画します。
// strcmpで前回描画済みの文字列と比較し、同じ文字列なら描画をスキップ。
// これにより、秒が変わった時だけ描画が実行され、ちらつきがありません。
// ================================================================
void drawClock()
{
  struct tm ti;
  // NTP同期がまだ完了していない場合は描画しない（時刻が取れないので意味がない）
  if (!getLocalTime(&ti))
    return;

  // 現在時刻を文字列に変換（例: "14:30:05"）
  char timeStr[16];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &ti);

  // 前回と同じ文字列なら描画をスキップ（ちらつき防止の核心部分）
  if (strcmp(timeStr, prevTimeStr) == 0)
    return;
  strcpy(prevTimeStr, timeStr); // 今回描画したので、キャッシュを更新

  // 時計エリアの背景色を一時的に適用
  clearArea(AREA_CLOCK_Y, AREA_CLOCK_H, COL_BG_CLOCK);

  // 文字色を緑、フォントサイズを2倍にして時刻を描画
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(15, 6);
  tft.print(timeStr);
  Serial.printf("[Clock] %s\n", timeStr); // デバッグ用: 描画が発生した秒を確認
}

// ================================================================
// 都市名エリアを描画
// 都市切替のタイミングだけで呼ばれます（頻度が低いのでキャッシュ不要）。
// 日本語フォントライブラリ(U8g2)を使って都市名を表示しています。
// ================================================================
void drawCity()
{
  // 注: drawWeatherInfo() が背景を一括で塗りつぶしているので、
  // ここでは個別にクリア（黒塗り）しません。

  // 日本語フォントをセットして都市名を描画
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  u8g2.setForegroundColor(ST77XX_YELLOW);

  // Stringを一度変数に格納して、描画関数に渡す（メモリ確保の安定化）
  String label = String(cities[cityIndex].name);
  u8g2.setCursor(8, AREA_CITY_Y + AREA_CITY_H - 4);   // ベースライン: エリア下端から4px上
  u8g2.print(label.c_str());

  // 都市名の下に仕切り線を描画（黒背景化を防ぐため、背景に馴染む色を使用）
  tft.drawFastHLine(0, AREA_CITY_Y + AREA_CITY_H + 2, 128, 0x4208);

  Serial.printf("[City] %s\n", cities[cityIndex].name); // デバッグ用: 都市切替を確認
}

// ================================================================
// 気温エリアを描画
// 天気アイコン（左）と気温（右）を表示します。
// 天気データ更新時・都市切替時に呼ばれます。
// ================================================================
void drawTemp()
{
  // 注: drawWeatherInfo() が背景を一括で塗りつぶしているので、
  // ここでは個別にクリアしません。

  // 天気アイコンを左側に描画
  drawWeatherIcon(8, AREA_TEMP_Y + 4, currentWeatherCode);

  // 気温をアイコンの右 side に描画（アイコンと重ならないようにX座標を48にシフト）
  u8g2.setFont(u8g2_font_logisoso24_tr);    // アイコンと並べるためサイズダウン（32→24）
  u8g2.setForegroundColor(ST77XX_CYAN);
  u8g2.setCursor(48, AREA_TEMP_Y + AREA_TEMP_H - 18);

  // 気温を小数点1桁で文字列化（例: "25.3"）
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", currentTemp);
  u8g2.print(buf);

  // 「度」だけ日本語フォントに切り替えて続けて描画
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  u8g2.print("度");

  // 気温の下の仕切り線を描画
  tft.drawFastHLine(0, AREA_TEMP_Y + AREA_TEMP_H + 2, 128, 0x4208);
}

// ================================================================
// 天気説明エリアを描画
// 天気コードを日本語に変換したテキストを描画します。
// 天気データ更新時・都市切替時に呼ばれます。
// ================================================================
void drawWeather()
{
  // 注: drawWeatherInfo() が背景を一括で塗りつぶしているので、
  // ここでは個別にクリアしません。

  u8g2.setFont(u8g2_font_b16_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(8, AREA_WEATHER_Y + AREA_WEATHER_H - 12);  // 少し上に調整

  String ws = getWeatherJp(currentWeatherCode);
  u8g2.print(ws.c_str());

  // 境界線（アクセント）を描画
  //tft.drawFastHLine(4, AREA_WEATHER_Y - 4, 120, 0x4208);
}

// ================================================================
// 気温・天気説明をまとめて更新（差分描画対応済み）
// 都市切替時・API通信でデータが变化した時に呼びます。
// 全体の処理の流れ:
//   1. 天気に応じた背景色を取得して画面を塗りつぶす
//   2. 文字と背景色の色を一致させる（黒い枠の残りを防ぐ）
//   3. 都市名・気温・天気説明を一筆描画
//   4. キャッシュに現在値を記録（次回差分チェック用）
// ================================================================
void drawWeatherInfo()
{
  // 天気に応じた背景色を取得
  uint16_t bg = getBgColor(currentWeatherCode);

  // 時計エリアの下から画面最下部までを一括で背景色に塗りつぶす。
  // これにより、文字やラインの隙間に黒色が残るのを防げます。
  tft.fillRect(0, AREA_CITY_Y, 128, 160 - AREA_CITY_Y, bg);

  // 日本語フォントの背景色を画面の背景色と一致させる。
  // U8g2はデフォルトが黒の背景なので、これだと文字の後ろが黒四角になる。
  u8g2.setBackgroundColor(bg);

  // 各エリアを描画
  drawCity();
  drawTemp();
  drawWeather();

  // キャッシュを現在値で更新（次回、差分チェックで「前と同じ値」なら
  // スキップするための記録）
  prevTemp = currentTemp;
  prevWeatherCode = currentWeatherCode;
  Serial.printf("[Weather] Temp: %.1f, Code: %d (%s)\n",
                currentTemp, currentWeatherCode,
                getWeatherJp(currentWeatherCode).c_str()); // デバッグ用
}

// ================================================================
// 起動時に一度だけ呼ぶ静的要素の描画
// 仕切り線など、変更されない要素を描画します。
// 初期表示時に1回だけ呼び、以後は変更しません。
// ================================================================
void drawStaticElements()
{
  tft.fillScreen(ST77XX_BLACK);
  tft.drawFastHLine(0, 30, 128, ST77XX_WHITE);  // 時計エリアと情報エリアの区切り線
}
