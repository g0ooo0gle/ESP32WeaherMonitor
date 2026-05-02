/**
 * ESP32 Weather Station - 天気関連機能
 *
 * [変更点]
 *   - WeeklyForecast 構造体を新設（週間天気データ格納用）
 *   - weeklyForecast[] 配列と weeklyDays 変数を extern 宣言
 *   - drawWeeklyForecast() 関数を宣言
 */

#ifndef WEATHER_H
#define WEATHER_H

#include "config.h"

// ------------------------------------------------------------------
// 週間天気予報の1日分データ構造体
// Open-Meteo の daily API から取得した値を格納します。
// ------------------------------------------------------------------
struct DailyForecast {
  int   weatherCode;   // WMO天気コード（その日の代表天気）
  float tempMax;       // 最高気温（℃）
  float tempMin;       // 最低気温（℃）
  char  label[6];      // 表示ラベル（例: "今日", "明日", "火", "水" ...）
};

// 週間天気の表示日数（今日含む最大7日分。画面サイズの都合で6日を使用）
#define WEEKLY_DAYS 6

// 週間天気データの実体（network.cpp で更新、display.cpp で参照）
extern DailyForecast weeklyForecast[WEEKLY_DAYS];
extern int           weeklyDays;   // 実際に取得できた日数（最大 WEEKLY_DAYS）

// ------------------------------------------------------------------
// WMO天気コードを日本語テキストに変換
// ------------------------------------------------------------------
String getWeatherJp(int code);

// ------------------------------------------------------------------
// 天気コードから背景色を判定（RGB565形式で返す）
// ------------------------------------------------------------------
uint16_t getBgColor(int code);

// ------------------------------------------------------------------
// 天気アイコンを描画（36×36px の範囲内）
//   x, y : アイコン領域の左上座標
//   code : WMO天気コード
// ------------------------------------------------------------------
void drawWeatherIcon(int x, int y, int code);

// ------------------------------------------------------------------
// 週間天気を画面に描画する
// AREA_WEEKLY_Y 〜 AREA_WEEKLY_Y+AREA_WEEKLY_H の範囲に6日分を表示。
// ------------------------------------------------------------------
void drawWeeklyForecast();

#endif // WEATHER_H
