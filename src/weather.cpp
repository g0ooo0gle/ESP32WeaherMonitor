/**
 * weather.cpp - 天気関連機能実装
 *
 * WMO 天気コードの変換・背景色判定・アイコン描画・
 * 週間/毎時予報の描画を担当します。
 *
 * [フォント]
 *   日本語テキストはすべて u8g2_font_b??_t_japanese3 サブセットを使用します。
 *   JIS 第 1 水準の大部分を収録しており、「概ね」「霧雨」など
 *   画数の多い漢字も正しく表示できます。
 *
 * [詳細エリアのレイアウト]
 *   1 行の高さ = 13px × 6 行 = 78px（AREA_DETAIL_H=82px に余裕を持たせる）
 */

#include "weather.h"

DailyForecast  weeklyForecast[WEEKLY_DAYS];
int            weeklyDays = 0;

HourlyForecast hourlyForecast[HOURLY_HOURS];
int            hourlyHours = 0;

// ================================================================
// WMO 天気コードを日本語テキストに変換
// ================================================================
String getWeatherJp(int code)
{
  switch (code) {
  case 0:  return "快晴";
  case 1:  return "概ね晴れ";
  case 2:  return "時々曇り";
  case 3:  return "くもり";
  case 45: return "霧";
  case 48: return "着氷性の霧";
  case 51: return "軽い霧雨";
  case 53: return "霧雨";
  case 55: return "濃い霧雨";
  case 56: return "軽い氷霧雨";
  case 57: return "濃い氷霧雨";
  case 61: return "小雨";
  case 63: return "雨";
  case 65: return "強い雨";
  case 66: return "軽い氷雨";
  case 67: return "強い氷雨";
  case 71: return "小雪";
  case 73: return "雪";
  case 75: return "強い雪";
  case 77: return "粒雪";
  case 80: return "にわか雨(弱)";
  case 81: return "にわか雨";
  case 82: return "激しい雨";
  case 85: return "軽い雪のシャワー";
  case 86: return "激しい雪のシャワー";
  case 95: return "雷雨";
  case 96: return "雷雨と軽い雹";
  case 99: return "雷雨と激しい雹";
  default: return "不明(" + String(code) + ")";
  }
}

// ================================================================
// 天気コードから背景色を返す (RGB565)
// ================================================================
uint16_t getBgColor(int code)
{
  if (code == 0)                                                     return COL_BG_CLEAR;
  if (code >= 1  && code <= 3)                                       return COL_BG_CLOUDY;
  if (code == 45 || code == 48)                                      return COL_BG_FOG;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82))     return COL_BG_RAIN;
  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86))     return COL_BG_SNOW;
  if (code >= 95)                                                    return COL_BG_THUNDER;
  return ST77XX_BLACK;
}

// ================================================================
// 気温＋°C を描画するヘルパー
// ================================================================
void drawTempWithUnit(int x, int y, float temp, uint16_t color, bool bigFont)
{
  u8g2.setFontMode(1);
  u8g2.setFont(bigFont ? u8g2_font_helvB18_tf : u8g2_font_helvB12_tf);
  u8g2.setForegroundColor(color);

  char buf[12];
  snprintf(buf, sizeof(buf), "%.1f\xC2\xB0""C", temp);   // ° = UTF-8 0xC2 0xB0
  u8g2.setCursor(x, y);
  u8g2.print(buf);
}

// ================================================================
// [内部] 雲を描画
// ================================================================
static void drawCloud(int cx, int cy, uint16_t color)
{
  tft.fillCircle(cx - 8, cy + 3, 6, color);
  tft.fillCircle(cx,     cy - 1, 8, color);
  tft.fillCircle(cx + 8, cy + 3, 6, color);
  tft.fillRect(cx - 14, cy + 3, 28, 6, color);
}

// ================================================================
// [内部] 太陽を描画
// ================================================================
static void drawSun(int cx, int cy)
{
  tft.fillCircle(cx, cy, 8, ST77XX_ORANGE);
  for (int deg = 0; deg < 360; deg += 45) {
    float rad = deg * DEG_TO_RAD;
    tft.drawLine(
      cx + (int)(cos(rad) * 11), cy + (int)(sin(rad) * 11),
      cx + (int)(cos(rad) * 15), cy + (int)(sin(rad) * 15),
      ST77XX_YELLOW
    );
  }
}

// ================================================================
// [内部] 小アイコン (16×12px) を描画
// ================================================================
static void drawSmallWeatherIcon(int x, int y, int code)
{
  int cx = x + 8;
  int cy = y + 6;

  if (code == 0) {
    tft.fillCircle(cx, cy, 4, ST77XX_ORANGE);
    for (int d = 0; d < 360; d += 45) {
      float r = d * DEG_TO_RAD;
      tft.drawLine(cx + (int)(cos(r)*6), cy + (int)(sin(r)*6),
                   cx + (int)(cos(r)*8), cy + (int)(sin(r)*8), ST77XX_YELLOW);
    }
  }
  else if (code == 1) {
    tft.fillCircle(cx - 2, cy - 1, 3, ST77XX_ORANGE);
    tft.fillCircle(cx + 2, cy + 2, 4, 0xC618);
    tft.fillRect(cx - 2, cy + 2, 8, 3, 0xC618);
  }
  else if (code == 2 || code == 3) {
    uint16_t c = (code == 2) ? 0xC618 : 0x8410;
    tft.fillCircle(cx - 3, cy + 1, 3, c);
    tft.fillCircle(cx + 2, cy - 1, 4, c);
    tft.fillCircle(cx + 6, cy + 1, 3, c);
    tft.fillRect(cx - 6, cy + 1, 12, 3, c);
  }
  else if (code == 45 || code == 48) {
    tft.drawFastHLine(x + 2, cy - 1, 12, 0x8410);
    tft.drawFastHLine(x + 2, cy + 3, 12, 0x8410);
  }
  else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    tft.fillCircle(cx, cy - 2, 4, 0x8410);
    tft.fillRect(cx - 4, cy - 2, 8, 3, 0x8410);
    int drops = (code == 51 || code == 61 || code == 80) ? 2 : 3;
    for (int i = 0; i < drops; i++) {
      tft.drawLine(x + 4 + i*4, cy + 3, x + 3 + i*4, cy + 7, ST77XX_CYAN);
    }
  }
  else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
    tft.fillCircle(cx, cy - 2, 4, 0x9CF3);
    tft.fillRect(cx - 4, cy - 2, 8, 3, 0x9CF3);
    for (int i = 0; i < 2; i++) {
      int sx = x + 3 + i * 7;
      int sy = cy + 5;
      tft.drawLine(sx, sy - 2, sx, sy + 2, ST77XX_WHITE);
      tft.drawLine(sx - 2, sy, sx + 2, sy, ST77XX_WHITE);
    }
  }
  else if (code >= 95) {
    tft.fillCircle(cx, cy - 2, 4, 0x4208);
    tft.fillRect(cx - 4, cy - 2, 8, 3, 0x4208);
    tft.fillTriangle(cx + 1, cy + 2, cx - 2, cy + 6, cx + 1, cy + 6, ST77XX_YELLOW);
    tft.fillTriangle(cx,     cy + 5, cx + 3, cy + 5, cx - 1, cy + 9, ST77XX_YELLOW);
  }
  else {
    tft.drawPixel(cx, cy, ST77XX_WHITE);
  }
}

// ================================================================
// 大きい天気アイコン描画 (36×36px)
// ================================================================
void drawWeatherIcon(int x, int y, int code)
{
  if (code == 0) {
    drawSun(x + 18, y + 18);
  }
  else if (code == 1) {
    drawSun(x + 12, y + 13);
    drawCloud(x + 20, y + 24, 0xC618);
  }
  else if (code == 2) {
    drawCloud(x + 18, y + 20, 0xC618);
  }
  else if (code == 3) {
    drawCloud(x + 18, y + 20, 0x8410);
  }
  else if (code == 45 || code == 48) {
    for (int i = 0; i < 3; i++)
      tft.drawRoundRect(x + 4, y + 12 + i * 8, 28, 3, 1, 0x8410);
  }
  else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    drawCloud(x + 18, y + 14, 0x8410);
    int drops;
    if      (code == 51 || code == 61 || code == 80) drops = 2;
    else if (code == 53 || code == 63 || code == 81) drops = 3;
    else                                              drops = 4;
    for (int i = 0; i < drops; i++) {
      int rx = x + 8 + i * 7;
      tft.drawLine(rx, y + 24, rx - 2, y + 31, ST77XX_CYAN);
    }
  }
  else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
    drawCloud(x + 18, y + 14, 0x9CF3);
    int snowX[] = {x + 9, x + 18, x + 27};
    for (int i = 0; i < 3; i++) {
      int sx = snowX[i], sy = y + 29;
      tft.drawLine(sx, sy - 3, sx, sy + 3, ST77XX_WHITE);
      tft.drawLine(sx - 3, sy, sx + 3, sy, ST77XX_WHITE);
      tft.drawLine(sx - 2, sy - 2, sx + 2, sy + 2, 0xB5B6);
      tft.drawLine(sx + 2, sy - 2, sx - 2, sy + 2, 0xB5B6);
    }
  }
  else if (code >= 95) {
    drawCloud(x + 18, y + 14, 0x4208);
    tft.fillTriangle(x + 21, y + 20, x + 14, y + 29, x + 20, y + 29, ST77XX_YELLOW);
    tft.fillTriangle(x + 19, y + 27, x + 24, y + 27, x + 15, y + 36, ST77XX_YELLOW);
  }
  else {
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.setCursor(x + 13, y + 12);
    tft.print("?");
  }
}

// ================================================================
// 週間天気を詳細エリアに描画
//
// レイアウト (1 行 = 13px × 6 行):
//   x=  2 : 小アイコン (16×12px)
//   x= 22 : ラベル（今日/明日/曜日）
//   x= 52 : 最高気温（赤）
//   x= 76 : "/"
//   x= 84 : 最低気温（水色）
//   x=108 : °C
// ================================================================
void drawWeeklyForecast()
{
  if (weeklyDays == 0) return;

  tft.fillRect(0, AREA_DETAIL_Y, SCREEN_W, AREA_DETAIL_H, ST77XX_BLACK);

  const int rowH = 13;
  u8g2.setFontMode(1);

  for (int i = 0; i < weeklyDays && i < WEEKLY_DAYS; i++)
  {
    int rowY = AREA_DETAIL_Y + i * rowH;

    drawSmallWeatherIcon(2, rowY, weeklyForecast[i].weatherCode);

    u8g2.setFont(u8g2_font_b12_b_t_japanese3);
    u8g2.setForegroundColor(ST77XX_YELLOW);
    u8g2.setCursor(22, rowY + 11);
    u8g2.print(weeklyForecast[i].label);

    char buf[8];
    u8g2.setFont(u8g2_font_helvR08_tf);
    u8g2.setForegroundColor(0xF800);
    u8g2.setCursor(52, rowY + 10);
    snprintf(buf, sizeof(buf), "%.0f", weeklyForecast[i].tempMax);
    u8g2.print(buf);

    u8g2.setForegroundColor(ST77XX_WHITE);
    u8g2.setCursor(76, rowY + 10);
    u8g2.print("/");

    u8g2.setForegroundColor(ST77XX_CYAN);
    u8g2.setCursor(84, rowY + 10);
    snprintf(buf, sizeof(buf), "%.0f", weeklyForecast[i].tempMin);
    u8g2.print(buf);

    u8g2.setFont(u8g2_font_helvB08_tf);
    u8g2.setForegroundColor(0x8410);
    u8g2.setCursor(108, rowY + 10);
    u8g2.print("\xC2\xB0""C");

    if (i < weeklyDays - 1)
      tft.drawFastHLine(20, rowY + rowH - 1, SCREEN_W - 20, 0x2104);
  }
}

// ================================================================
// 毎時天気を詳細エリアに描画
//
// レイアウト (1 行 = 13px × 6 行):
//   x=  2 : 小アイコン
//   x= 22 : 時刻ラベル（"今" または "14時"）
//   x= 68 : 気温（現在時刻のみオレンジ）
//   x=100 : °C
// ================================================================
void drawHourlyForecast()
{
  tft.fillRect(0, AREA_DETAIL_Y, SCREEN_W, AREA_DETAIL_H, ST77XX_BLACK);

  if (hourlyHours == 0) {
    u8g2.setFontMode(1);
    u8g2.setFont(u8g2_font_b12_b_t_japanese3);
    u8g2.setForegroundColor(ST77XX_WHITE);
    u8g2.setCursor(10, AREA_DETAIL_Y + 40);
    u8g2.print("毎時天気 取得中...");
    return;
  }

  const int rowH = 13;
  u8g2.setFontMode(1);

  for (int i = 0; i < hourlyHours && i < HOURLY_HOURS; i++)
  {
    int rowY = AREA_DETAIL_Y + i * rowH;

    drawSmallWeatherIcon(2, rowY, hourlyForecast[i].weatherCode);

    u8g2.setFont(u8g2_font_b12_b_t_japanese3);
    u8g2.setForegroundColor(ST77XX_YELLOW);
    u8g2.setCursor(22, rowY + 11);
    u8g2.print(hourlyForecast[i].label);

    char buf[8];
    u8g2.setFont(u8g2_font_helvR08_tf);
    u8g2.setForegroundColor(i == 0 ? ST77XX_ORANGE : ST77XX_WHITE);
    u8g2.setCursor(68, rowY + 10);
    snprintf(buf, sizeof(buf), "%.1f", hourlyForecast[i].temp);
    u8g2.print(buf);

    u8g2.setFont(u8g2_font_helvB08_tf);
    u8g2.setForegroundColor(0x8410);
    u8g2.setCursor(100, rowY + 10);
    u8g2.print("\xC2\xB0""C");

    if (i < hourlyHours - 1)
      tft.drawFastHLine(20, rowY + rowH - 1, SCREEN_W - 20, 0x2104);
  }
}
