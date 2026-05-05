/**
 * display.h - 天気画面描画ヘッダ
 *
 * WEATHER 画面を構成するエリア（時計・現在天気・詳細）の
 * 描画関数と、関連する状態変数を公開します。
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "config.h"
#include "weather.h"

// ------------------------------------------------------------------
// 状態管理変数
// ------------------------------------------------------------------
extern int      cityIndex;
extern float    currentTemp;
extern int      currentWeatherCode;
extern uint16_t currentBgColor;

// ------------------------------------------------------------------
// 差分描画キャッシュ
// ------------------------------------------------------------------
extern char  prevTimeStr[16];
extern int   prevCityIndex;
extern float prevTemp;
extern int   prevWeatherCode;

// ------------------------------------------------------------------
// タイマー管理変数
// ------------------------------------------------------------------
extern unsigned long lastFetchAttempt;
extern unsigned long lastWeeklyFetch;
extern unsigned long lastHourlyFetch;
extern unsigned long lastClockUpdate;

// ------------------------------------------------------------------
// エリア塗りつぶしヘルパー
// ------------------------------------------------------------------
inline void clearArea(int y, int h, uint16_t color = ST77XX_BLACK)
{
  tft.fillRect(0, y, SCREEN_W, h, color);
}

// ------------------------------------------------------------------
// WEATHER 画面の各エリア描画関数
// ------------------------------------------------------------------

/** 時計＋都市名エリア（差分検出付き） */
void drawClockCity();

/** 現在天気エリア（アイコン・気温・テキスト） */
void drawCurrentWeather();

/** 詳細エリア（週間 or 毎時をモードに応じて切替） */
void drawDetailArea();

/** WEATHER 画面全体を再描画 */
void drawWeatherInfo();

/** 起動時に 1 回だけ呼ぶ静的要素描画（区切り線など） */
void drawStaticElements();

/**
 * ローディングオーバーレイを画面中央に表示する。
 * 次の drawWeatherInfo() / drawNewsScreen() 呼び出しで自動的に消える。
 */
void showLoadingOverlay(const char* msg);

#endif // DISPLAY_H
