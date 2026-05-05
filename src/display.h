/**
 * ESP32 Weather Station - 描画機能 ヘッダ
 *
 * [今回の変更点]
 *   - drawNewsScreen() / drawWeatherScreen() を新設
 *     画面切替時にどちらかを呼ぶ。
 *   - 旧 drawWeatherInfo() は drawWeatherScreen() の別名として残します
 *     (既存コードからの呼び出しを変えなくてよいように)
 *   - 日本語フォントは weather.cpp と同じく japanese3 系で統一
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
extern uint16_t currentBgColor;   // 現在の天気背景色（時計エリア・差分描画に使用）

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
extern unsigned long lastCitySwitch;
extern unsigned long lastClockUpdate;

// ------------------------------------------------------------------
// エリア塗りつぶしヘルパー
// ------------------------------------------------------------------
inline void clearArea(int y, int h, uint16_t color = ST77XX_BLACK)
{
  tft.fillRect(0, y, SCREEN_W, h, color);
}

// ------------------------------------------------------------------
// WEATHER画面の各エリア描画関数
// ------------------------------------------------------------------

/** 時計＋都市名 */
void drawClockCity();

/** 現在天気エリア (アイコン+気温+詳細) */
void drawCurrentWeather();

/** 詳細エリア (週間 or 毎時を切替) */
void drawDetailArea();

/**
 * WEATHER画面全体を再描画
 * 旧名 drawWeatherInfo() のまま（既存呼び出し維持）
 */
void drawWeatherInfo();

/** drawWeatherInfo の別名（読みやすさ優先で main から呼ぶ用） */
inline void drawWeatherScreen() { drawWeatherInfo(); }

/** 起動時に1回だけ呼ぶ静的要素描画 (区切り線など) */
void drawStaticElements();

/**
 * ローディングオーバーレイを画面中央に表示する。
 * 次の drawWeatherInfo() / drawNewsScreen() 呼び出しで自動的に消える。
 */
void showLoadingOverlay(const char* msg);

#endif // DISPLAY_H
