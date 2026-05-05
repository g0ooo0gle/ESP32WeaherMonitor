/**
 * ESP32 Weather Station - 設定ファイル
 *
 * このファイルはプロジェクト全体の定数定義を一元管理しています。
 *
 * [画面レイアウト 128×160px]
 *
 *   ◆ WEATHER画面
 *     Y=  0〜29  : 時計＋都市名エリア (30px)
 *     Y= 30      : 区切り線
 *     Y= 32〜75  : 現在天気エリア (44px)
 *     Y= 76      : 区切り線
 *     Y= 78〜159 : 詳細エリア (82px、週間6日 or 毎時6時間)
 *
 *   ◆ NEWS画面
 *     Y=  0〜19  : タイトルバー (20px、"ニュース 3/12" 等)
 *     Y= 20      : 区切り線
 *     Y= 22〜111 : 見出し静止エリア (90px、折り返し最大5行)
 *     Y=112      : 仕切り線
 *     Y=114〜159 : 電光掲示板スクロールエリア (46px)
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <U8g2_for_Adafruit_GFX.h>

// ------------------------------------------------------------------
// ピン配置（ESP32の標準SPI = VSPI ピンを使用）
// ------------------------------------------------------------------
#define TFT_SCLK 18
#define TFT_MOSI 23
#define TFT_RST   4
#define TFT_DC    2
#define TFT_CS    5

// ------------------------------------------------------------------
// ディスプレイインスタンス（main.cpp で実体定義、他は extern 参照）
// ------------------------------------------------------------------
extern Adafruit_ST7735 tft;
extern U8G2_FOR_ADAFRUIT_GFX u8g2;

// ------------------------------------------------------------------
// 画面サイズ
// ------------------------------------------------------------------
#define SCREEN_W  128
#define SCREEN_H  160

// ------------------------------------------------------------------
// WEATHER画面のレイアウト
//
// ティッカー領域を廃止し、詳細エリアを画面下端まで拡張。
// 1行の高さは AREA_DETAIL_H / WEEKLY_DAYS = 82/6 ≒ 13.7px。
// 整数で扱うため 13px として最終行に少し余裕を持たせます。
// ------------------------------------------------------------------
#define AREA_CLOCK_Y      0
#define AREA_CLOCK_H     30

#define AREA_WEATHER_Y   32
#define AREA_WEATHER_H   44

#define AREA_DETAIL_Y    78
#define AREA_DETAIL_H    82

#define LINE_Y1   30
#define LINE_Y2   76

// ------------------------------------------------------------------
// NEWS画面のレイアウト
// ------------------------------------------------------------------
#define NEWS_TITLE_Y      0
#define NEWS_TITLE_H     20
#define NEWS_LINE_Y      20

// 見出し静止エリア (上部 2/3)
#define NEWS_STATIC_Y    22
#define NEWS_STATIC_H    90

// 静止エリアと電光掲示板の仕切り線
#define NEWS_DIVIDER_Y  112

// 電光掲示板スクロールエリア (下部 1/3)
#define NEWS_TICKER_Y   114
#define NEWS_TICKER_H   (SCREEN_H - NEWS_TICKER_Y)   // 46px

// ------------------------------------------------------------------
// 背景色 (RGB565形式)
// ------------------------------------------------------------------
#define COL_BG_CLEAR    0x0952
#define COL_BG_RAIN     0x0926
#define COL_BG_SNOW     0x0E46
#define COL_BG_THUNDER  0x08A5
#define COL_BG_FOG      0x0904
#define COL_BG_CLOUDY   0x08E4
#define COL_BG_CLOCK    0x00A4
#define COL_BG_NEWS     0x10A2   // ニュース画面: 暗い紫
#define COL_BG_NEWSBAR  0x4208   // ニュースタイトルバー: 中グレー

// ------------------------------------------------------------------
// タイマー間隔 (ミリ秒)
// ------------------------------------------------------------------

// 現在天気の取得間隔: 15分
const unsigned long fetchInterval       = 15UL * 60UL * 1000UL;

// 週間天気の取得間隔: 1時間
const unsigned long weeklyFetchInterval = 60UL * 60UL * 1000UL;

// 毎時天気の取得間隔: 30分
const unsigned long hourlyFetchInterval = 30UL * 60UL * 1000UL;

// 全国巡回モードでの都市切替間隔: 20秒
const unsigned long citySwitchInterval  = 20000UL;

// 時計更新間隔: 1秒チェック（差分検出により実際の描画は分変化時のみ）
const unsigned long clockInterval       = 1000UL;

#endif // CONFIG_H
