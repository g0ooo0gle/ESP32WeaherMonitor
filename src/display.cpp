/**
 * ESP32 Weather Station - 描画機能 実装
 *
 * [今回の変更点]
 *   - 日本語フォントを b16/b12_t_japanese3 に統一
 *     (天気詳細「概ね晴れ」など画数の多い漢字も化けません)
 *   - 詳細エリアの新しい高さ (82px) に合わせてレイアウトを微調整
 *   - 旧 drawWeatherInfo() はそのまま使えます (drawWeatherScreen の実体)
 */

#include "display.h"
#include "cities.h"
#include "button.h"
#include "settings.h"

// ================================================================
// 状態管理変数の実体定義
// ================================================================
int   cityIndex          = 0;
float currentTemp        = 0;
int   currentWeatherCode = 0;

// ================================================================
// 差分検出用キャッシュの実体
// ================================================================
char        prevTimeStr[16]  = "";
int         prevCityIndex    = -1;
float       prevTemp         = -999;
int         prevWeatherCode  = -1;
DisplayMode prevMode         = DisplayMode::ALL_CITIES;

// ================================================================
// タイマー管理変数の実体
// ================================================================
unsigned long lastFetchAttempt = 0;
unsigned long lastWeeklyFetch  = 0;
unsigned long lastHourlyFetch  = 0;
unsigned long lastCitySwitch   = 0;
unsigned long lastClockUpdate  = 0;

// ================================================================
// 時計＋都市名エリア (差分描画対応)
// ================================================================
void drawClockCity()
{
  struct tm ti;
  if (!getLocalTime(&ti)) return;

  char timeStr[16];
  strftime(timeStr, sizeof(timeStr), "%H:%M", &ti);

  // 時刻・都市・モードがすべて同じなら描画スキップ
  if (strcmp(timeStr, prevTimeStr) == 0 && prevCityIndex == cityIndex && prevMode == currentMode) return;

  strcpy(prevTimeStr, timeStr);
  prevMode = currentMode;

  // エリア塗りつぶし
  tft.fillRect(0, AREA_CLOCK_Y, SCREEN_W, AREA_CLOCK_H, COL_BG_CLOCK);

  // ---- 時刻 (helvB18, °対応フォント) ----
  u8g2.setFontMode(1);
  u8g2.setFont(u8g2_font_helvB18_tf);
  u8g2.setForegroundColor(ST77XX_GREEN);
  u8g2.setCursor(2, AREA_CLOCK_Y + 23);
  u8g2.print(timeStr);

  // ---- 都市名 (japanese3) ----
  u8g2.setFont(u8g2_font_b12_t_japanese3);
  u8g2.setForegroundColor(ST77XX_YELLOW);
  u8g2.setCursor(72, AREA_CLOCK_Y + AREA_CLOCK_H - 8);

  // 1都市モード時は ★ で強調
  if (currentMode == DisplayMode::SINGLE) {
    u8g2.print("\xE2\x98\x85");   // UTF-8 の ★
  }
  u8g2.print(getActiveName());

  Serial.printf("[Clock] %s  %s\n", timeStr, getActiveName());
}

// ================================================================
// 現在天気エリア (アイコン+気温+詳細を1エリアに統合)
// ================================================================
void drawCurrentWeather()
{
  // ---- 天気アイコン ----
  drawWeatherIcon(2, AREA_WEATHER_Y + 4, currentWeatherCode);

  // ---- 気温＋°C (大きいフォント、°対応) ----
  drawTempWithUnit(44, AREA_WEATHER_Y + 22, currentTemp, ST77XX_CYAN, true);

  // ---- 天気詳細テキスト (japanese3 フォント) ----
  u8g2.setFontMode(1);
  u8g2.setFont(u8g2_font_b12_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(44, AREA_WEATHER_Y + 40);
  String ws = getWeatherJp(currentWeatherCode);
  u8g2.print(ws.c_str());
}

// ================================================================
// 詳細エリア描画 (週間 or 毎時)
// ================================================================
void drawDetailArea()
{
  if (currentMode == DisplayMode::SINGLE && currentSub == SubView::HOURLY) {
    drawHourlyForecast();
  } else {
    drawWeeklyForecast();
  }
}

// ================================================================
// WEATHER画面全体を再描画
// ================================================================
void drawWeatherInfo()
{
  uint16_t bg = getBgColor(currentWeatherCode);

  // 時計エリアの下から画面下端までを天気色で塗りつぶし
  tft.fillRect(0, AREA_CLOCK_Y, SCREEN_W, SCREEN_H - AREA_CLOCK_Y, bg);

  u8g2.setBackgroundColor(bg);
  u8g2.setFontMode(1);

  // 各エリアを描画
  drawClockCity();
  drawCurrentWeather();
  drawDetailArea();

  // 区切り線 (背景塗りつぶしで消えるので再描画)
  tft.drawFastHLine(0, LINE_Y1, SCREEN_W, ST77XX_WHITE);
  tft.drawFastHLine(0, LINE_Y2, SCREEN_W, 0x4208);

  // キャッシュ更新
  prevTemp        = currentTemp;
  prevWeatherCode = currentWeatherCode;
  prevCityIndex   = cityIndex;

  Serial.printf("[Display] %s %.1f°C code=%d\n",
                getActiveName(), currentTemp, currentWeatherCode);
}

// ================================================================
// 起動時に1回だけ呼ぶ静的要素描画
// ================================================================
void drawStaticElements()
{
  tft.fillScreen(ST77XX_BLACK);
  tft.drawFastHLine(0, LINE_Y1, SCREEN_W, ST77XX_WHITE);
  tft.drawFastHLine(0, LINE_Y2, SCREEN_W, 0x4208);
}
