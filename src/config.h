/**
 * config.h - プロジェクト全体の定数定義
 *
 * [WEATHER 画面レイアウト 128×160px]
 *
 *   Y=  0〜29  : 時計＋都市名エリア (30px)
 *   Y= 30      : 区切り線
 *   Y= 32〜75  : 現在天気エリア (44px)
 *   Y= 76      : 区切り線
 *   Y= 78〜159 : 詳細エリア (82px、週間 6 日 or 毎時 6 時間)
 *
 * [NEWS 画面レイアウト]
 *   Y=  0〜19  : タイトルバー (20px)
 *   Y= 20      : 区切り線
 *   Y= 22〜111 : 見出し静止エリア (90px、最大 5 行折り返し)
 *   Y=112      : 仕切り線
 *   Y=114〜159 : 電光掲示板スクロールエリア (46px)
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <U8g2_for_Adafruit_GFX.h>

// ------------------------------------------------------------------
// ピン配置（ESP32 標準 SPI = VSPI）
// ------------------------------------------------------------------
#define TFT_SCLK 18
#define TFT_MOSI 23
#define TFT_RST   4
#define TFT_DC    2
#define TFT_CS    5

// ------------------------------------------------------------------
// ディスプレイインスタンス（main.cpp で実体定義）
// ------------------------------------------------------------------
extern Adafruit_ST7735      tft;
extern U8G2_FOR_ADAFRUIT_GFX u8g2;

// ------------------------------------------------------------------
// 画面サイズ
// ------------------------------------------------------------------
#define SCREEN_W  128
#define SCREEN_H  160

// ------------------------------------------------------------------
// WEATHER 画面レイアウト定数
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
// NEWS 画面レイアウト定数
// ------------------------------------------------------------------
#define NEWS_TITLE_Y      0
#define NEWS_TITLE_H     20
#define NEWS_LINE_Y      20

#define NEWS_STATIC_Y    22
#define NEWS_STATIC_H    90

#define NEWS_DIVIDER_Y  112

#define NEWS_TICKER_Y   114
#define NEWS_TICKER_H   (SCREEN_H - NEWS_TICKER_Y)   // 46px

// ------------------------------------------------------------------
// 背景色 (RGB565)
// ------------------------------------------------------------------
#define COL_BG_CLEAR    0x0952
#define COL_BG_RAIN     0x0926
#define COL_BG_SNOW     0x0E46
#define COL_BG_THUNDER  0x08A5
#define COL_BG_FOG      0x0904
#define COL_BG_CLOUDY   0x08E4
#define COL_BG_CLOCK    0x00A4
#define COL_BG_NEWS     0x10A2
#define COL_BG_NEWSBAR  0x4208

// ------------------------------------------------------------------
// タイマー間隔 (ミリ秒)
// ------------------------------------------------------------------
constexpr unsigned long fetchInterval       = 15UL * 60UL * 1000UL;  // 現在天気: 15 分
constexpr unsigned long weeklyFetchInterval = 60UL * 60UL * 1000UL;  // 週間予報: 1 時間
constexpr unsigned long hourlyFetchInterval = 30UL * 60UL * 1000UL;  // 毎時予報: 30 分
constexpr unsigned long clockInterval       = 1000UL;                // 時計チェック: 1 秒

#endif // CONFIG_H
