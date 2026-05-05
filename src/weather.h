/**
 * weather.h - 天気関連機能ヘッダ
 *
 * WMO 天気コードの変換・背景色判定・アイコン描画・
 * 週間/毎時予報データ構造と描画関数を公開します。
 */

#ifndef WEATHER_H
#define WEATHER_H

#include "config.h"

// ------------------------------------------------------------------
// 週間天気予報の 1 日分データ
// ------------------------------------------------------------------
struct DailyForecast {
  int   weatherCode;
  float tempMax;
  float tempMin;
  char  label[8];   // "今日" "明日" "月"〜"日" (UTF-8 日本語 + 終端)
};

#define WEEKLY_DAYS 6

extern DailyForecast weeklyForecast[WEEKLY_DAYS];
extern int           weeklyDays;

// ------------------------------------------------------------------
// 毎時天気予報の 1 時間分データ
// ------------------------------------------------------------------
struct HourlyForecast {
  int   weatherCode;
  float temp;
  char  label[8];   // "今" または "14時" など
};

#define HOURLY_HOURS 6

extern HourlyForecast hourlyForecast[HOURLY_HOURS];
extern int            hourlyHours;

// ------------------------------------------------------------------
// 関数宣言
// ------------------------------------------------------------------

/** WMO 天気コードを日本語テキストに変換 */
String getWeatherJp(int code);

/** 天気コードから背景色を返す (RGB565) */
uint16_t getBgColor(int code);

/** 天気アイコン描画 (36×36px) */
void drawWeatherIcon(int x, int y, int code);

/** 週間天気を詳細エリアに描画（bgColor: 詳細エリアの背景色） */
void drawWeeklyForecast(uint16_t bgColor);

/** 毎時天気を詳細エリアに描画（bgColor: 詳細エリアの背景色） */
void drawHourlyForecast(uint16_t bgColor);

/** 気温＋°C を描画するヘルパー（° 対応フォント使用） */
void drawTempWithUnit(int x, int y, float temp, uint16_t color, bool bigFont);

#endif // WEATHER_H
