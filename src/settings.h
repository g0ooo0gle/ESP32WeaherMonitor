/**
 * ESP32 Weather Station - 設定管理 ヘッダ
 *
 * NVS (Preferences) による設定の永続化と、
 * ブラウザから変更できる Web 設定ページを提供します。
 *
 * [保存する設定]
 *   myCityIndex  : 自分の都市 (0 〜 cityCount-1)
 *   regionFilter : 巡回する地方 (FILTER_xxx 定数)
 *
 * [Web 設定ページ]
 *   http://<ESP32のIP>/ でアクセス
 *   設定を変更して「保存」ボタンを押すと NVS に書き込まれ即時反映。
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
extern int  myCityIndex;      // 自分の都市インデックス (0 〜 cityCount-1)
extern int  regionFilter;     // 巡回する地方 (FILTER_xxx)
extern bool settingsChanged;  // Web 保存後に true → loop() で天気更新＆再描画

// カスタム都市（手動入力した都市名・緯度・経度）
extern char  customCityName[32];  // 都市名
extern float customCityLat;       // 緯度
extern float customCityLon;       // 経度
extern bool  useCustomCity;       // true = カスタム優先、false = 登録都市を使用

// ================================================================
// 自分の都市モードで使う値を返すアクセサ
// useCustomCity && currentMode==SINGLE のときカスタム値、それ以外は cities[cityIndex]
// ================================================================
const char* getActiveName();
float       getActiveLat();
float       getActiveLon();

// ================================================================
// 設定の読み書き
// ================================================================
void loadSettings();   // NVS から読み込む（setup() で呼ぶ）
void saveSettings();   // NVS へ書き込む

// ================================================================
// Web 設定サーバー
// ================================================================
void setupWebServer();   // WiFi 接続後に setup() で呼ぶ
void handleWebServer();  // loop() から毎回呼ぶ

// ================================================================
// 地方フィルタ対応ユーティリティ
// ================================================================

/** cityIdx の都市が現在の regionFilter にマッチするか */
bool cityMatchesFilter(int cityIdx);

/** current の次の都市インデックス（regionFilter で絞り込み、全周回） */
int getNextCityInRegion(int current);

/** regionFilter にマッチする最初の都市インデックス */
int getFirstCityInRegion();
