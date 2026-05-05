/**
 * settings.h - 設定管理ヘッダ
 *
 * NVS (Preferences) による設定の永続化と
 * ブラウザから変更できる Web 設定ページを提供します。
 *
 * [保存する設定]
 *   myCityIndex  : 自分の都市 (0 〜 cityCount-1)
 *   regionFilter : 巡回する地方 (FILTER_xxx 定数)
 *   useCustomCity / customCityName / customCityLat / customCityLon
 *
 * [Web 設定ページ]
 *   http://<ESP32 の IP>/ でアクセス
 */

#pragma once

#include "config.h"
#include "cities.h"

// ================================================================
// 巡回する地方フィルタ定数
// ================================================================
#define FILTER_ALL              (-1)  // 全国
#define FILTER_HOKKAIDO_TOHOKU    0   // 北海道・東北地方
#define FILTER_KANTO              1   // 関東地方
#define FILTER_CHUBU              2   // 中部地方
#define FILTER_KINKI              3   // 関西地方
#define FILTER_CHUGOKU            4   // 中国・四国地方
#define FILTER_KYUSHU             5   // 九州・沖縄地方

// ================================================================
// 設定変数（settings.cpp で実体定義）
// ================================================================
extern int  myCityIndex;
extern int  regionFilter;
extern bool settingsChanged;  // Web 設定保存後に true → loop() で再描画

extern char  customCityName[32];
extern float customCityLat;
extern float customCityLon;
extern bool  useCustomCity;

// ================================================================
// 自分の都市モードで使う値を返すアクセサ
// useCustomCity && SINGLE モードのときカスタム値、それ以外は cities[cityIndex]
// ================================================================
const char* getActiveName();
float       getActiveLat();
float       getActiveLon();

// ================================================================
// 設定の読み書き
// ================================================================
void loadSettings();
void saveSettings();

// ================================================================
// Web 設定サーバー
// ================================================================
void setupWebServer();
void handleWebServer();

// ================================================================
// 地方フィルタ対応ユーティリティ
// ================================================================
bool cityMatchesFilter(int cityIdx);
int  getNextCityInRegion(int current);
int  getFirstCityInRegion();
