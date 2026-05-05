/**
 * display.cpp - 天気画面描画実装
 *
 * WEATHER 画面を 3 エリアに分けて描画します。
 *   - 時計＋都市名エリア: 分単位の差分検出で不要な再描画を省略
 *   - 現在天気エリア: アイコン・気温・天気テキスト
 *   - 詳細エリア: 週間予報 or 毎時予報（モードに応じて切替）
 */

#include "display.h"
#include "cities.h"
#include "button.h"
#include "settings.h"

// ================================================================
// 状態管理変数の実体定義
// ================================================================
int      cityIndex          = 0;
float    currentTemp        = 0;
int      currentWeatherCode = 0;
uint16_t currentBgColor     = COL_BG_CLEAR;

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
// 時計＋都市名エリア（差分検出付き）
// 時刻・都市名・モードがすべて同じなら描画をスキップします。
// ================================================================
void drawClockCity()
{
  struct tm ti;
  if (!getLocalTime(&ti)) return;

  char timeStr[16];
  strftime(timeStr, sizeof(timeStr), "%H:%M", &ti);

  if (strcmp(timeStr, prevTimeStr) == 0 &&
      prevCityIndex == cityIndex &&
      prevMode == currentMode) return;

  strcpy(prevTimeStr, timeStr);
  prevCityIndex = cityIndex;
  prevMode      = currentMode;

  tft.fillRect(0, AREA_CLOCK_Y, SCREEN_W, AREA_CLOCK_H, currentBgColor);

  u8g2.setFontMode(1);
  u8g2.setFont(u8g2_font_helvB18_tf);
  u8g2.setForegroundColor(ST77XX_GREEN);
  u8g2.setCursor(2, AREA_CLOCK_Y + 23);
  u8g2.print(timeStr);

  u8g2.setFont(u8g2_font_b12_t_japanese3);
  u8g2.setForegroundColor(ST77XX_YELLOW);
  u8g2.setCursor(72, AREA_CLOCK_Y + AREA_CLOCK_H - 8);

  if (currentMode == DisplayMode::SINGLE) {
    u8g2.print("\xE2\x98\x85");  // UTF-8 の ★
  }
  u8g2.print(getActiveName());

  Serial.printf("[Clock] %s  %s\n", timeStr, getActiveName());
}

// ================================================================
// 現在天気エリア（アイコン・気温・テキスト）
// ================================================================
void drawCurrentWeather()
{
  drawWeatherIcon(2, AREA_WEATHER_Y + 4, currentWeatherCode);

  drawTempWithUnit(44, AREA_WEATHER_Y + 22, currentTemp, ST77XX_CYAN, true);

  u8g2.setFontMode(1);
  u8g2.setFont(u8g2_font_b12_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(44, AREA_WEATHER_Y + 40);
  u8g2.print(getWeatherJp(currentWeatherCode).c_str());
}

// ================================================================
// 詳細エリア（週間 or 毎時）
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
// WEATHER 画面全体を再描画
// ================================================================
void drawWeatherInfo()
{
  currentBgColor = getBgColor(currentWeatherCode);
  uint16_t bg    = currentBgColor;

  tft.fillRect(0, AREA_CLOCK_Y, SCREEN_W, SCREEN_H - AREA_CLOCK_Y, bg);

  u8g2.setBackgroundColor(bg);
  u8g2.setFontMode(1);

  // fillRect で時計エリアが消えるため、差分キャッシュをクリアして強制再描画
  prevTimeStr[0] = '\0';

  drawClockCity();
  drawCurrentWeather();
  drawDetailArea();

  tft.drawFastHLine(0, LINE_Y1, SCREEN_W, ST77XX_WHITE);
  tft.drawFastHLine(0, LINE_Y2, SCREEN_W, 0x4208);

  prevTemp        = currentTemp;
  prevWeatherCode = currentWeatherCode;
  prevCityIndex   = cityIndex;

  Serial.printf("[Display] %s %.1f°C code=%d\n",
                getActiveName(), currentTemp, currentWeatherCode);
}

// ================================================================
// ローディングオーバーレイを画面中央に表示
// ================================================================
void showLoadingOverlay(const char* msg)
{
  const int boxW = 110, boxH = 44, boxR = 5;
  const int boxX = (SCREEN_W - boxW) / 2;
  const int boxY = (SCREEN_H - boxH) / 2;

  tft.fillRoundRect(boxX, boxY, boxW, boxH, boxR, 0x2104);
  tft.drawRoundRect(boxX, boxY, boxW, boxH, boxR, ST77XX_WHITE);

  u8g2.setFont(u8g2_font_b12_t_japanese3);
  u8g2.setFontMode(0);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setBackgroundColor(0x2104);

  int tw = u8g2.getUTF8Width(msg);
  int tx = boxX + (boxW - tw) / 2;
  if (tx < boxX + 2) tx = boxX + 2;

  u8g2.setCursor(tx, boxY + 30);
  u8g2.print(msg);
  u8g2.setFontMode(1);
}

// ================================================================
// 起動時に 1 回だけ呼ぶ静的要素描画
// ================================================================
void drawStaticElements()
{
  tft.fillScreen(ST77XX_BLACK);
  tft.drawFastHLine(0, LINE_Y1, SCREEN_W, ST77XX_WHITE);
  tft.drawFastHLine(0, LINE_Y2, SCREEN_W, 0x4208);
}
