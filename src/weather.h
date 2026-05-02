/**
 * ESP32 Weather Station - 天気関連機能 ヘッダ
 *
 * [今回の変更点]
 *   - 構造体の中身は変わらず、描画関数の中で詳細エリアの新しい高さ
 *     (AREA_DETAIL_H = 82px) に合わせてレイアウトを再計算します。
 *   - 日本語フォントを b16/b12/b10/b08 _t_japanese3 に統一
 *     (japanese1 だと「概」など一部の常用漢字が欠落するため)
 */

#ifndef WEATHER_H
#define WEATHER_H

#include "config.h"

// ------------------------------------------------------------------
// 週間天気予報の1日分データ構造体
// ------------------------------------------------------------------
struct DailyForecast {
  int   weatherCode;   // WMO天気コード
  float tempMax;       // 最高気温
  float tempMin;       // 最低気温
  char  label[8];      // 表示ラベル (UTF-8 で日本語2文字+終端)
};

#define WEEKLY_DAYS 6

extern DailyForecast weeklyForecast[WEEKLY_DAYS];
extern int           weeklyDays;

// ------------------------------------------------------------------
// 毎時天気予報の1時間分データ構造体
// ------------------------------------------------------------------
struct HourlyForecast {
  int   weatherCode;
  float temp;
  char  label[8];      // "今" または "14時" など
};

#define HOURLY_HOURS 6

extern HourlyForecast hourlyForecast[HOURLY_HOURS];
extern int            hourlyHours;

// ------------------------------------------------------------------
// 関数宣言
// ------------------------------------------------------------------

/** WMO天気コードを日本語テキストに変換 */
String getWeatherJp(int code);

/** 天気コードから背景色を判定 (RGB565形式) */
uint16_t getBgColor(int code);

/** 天気アイコン描画 (36×36px) */
void drawWeatherIcon(int x, int y, int code);

/** 週間天気を詳細エリアに描画 */
void drawWeeklyForecast();

/** 毎時天気を詳細エリアに描画 */
void drawHourlyForecast();

/** 気温＋°C を描画するヘルパー (°対応フォント使用) */
void drawTempWithUnit(int x, int y, float temp, uint16_t color, bool bigFont);

#endif // WEATHER_H
