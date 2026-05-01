/**
 * ESP32 Weather Station - 設定ファイル
 *
 * このファイルはプロジェクト全体の定数定義を一元管理しています。
 * ピン配置、画面レイアウト、色、インターバルなどの「数値」を
 * ここに集めることで、後から値を変更するのが簡単になります。
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <U8g2_for_Adafruit_GFX.h>

// ------------------------------------------------------------------
// ピン配置
// ESP32の標準SPI（VSPI）ピンを使用しています。
// ハードウェア配線に合わせて変更してください。
// ------------------------------------------------------------------
#define TFT_SCLK 18   // SCL (Clock)
#define TFT_MOSI 23  // SDA (Data)
#define TFT_RST  4   // RESET
#define TFT_DC   2   // A0 (Data/Command)
#define TFT_CS   5   // CS (Chip Select)

// ------------------------------------------------------------------
// ディスプレイインスタンス
// main.cpp で外部参照します（extern宣言）
// ------------------------------------------------------------------
extern Adafruit_ST7735 tft;
extern U8G2_FOR_ADAFRUIT_GFX u8g2;

// ------------------------------------------------------------------
// 画面レイアウト定数
// 128x160ピクセルのディスプレイを4つのエリアに分割しています。
// ここを変更すると画面全体のレイアウトが変わります。
// ------------------------------------------------------------------
#define AREA_CLOCK_Y  0     // 時計エリア 開始Y座標
#define AREA_CLOCK_H  30    // 時計エリア 高さ
#define AREA_CITY_Y   31    // 都市名エリア 開始Y座標
#define AREA_CITY_H   24    // 都市名エリア 高さ
#define AREA_TEMP_Y   58    // 気温エリア 開始Y座標
#define AREA_TEMP_H   60    // 気温エリア 高さ
#define AREA_WEATHER_Y 122  // 天気説明エリア 開始Y座標
#define AREA_WEATHER_H 38   // 天気説明エリア 高さ

// ------------------------------------------------------------------
// 背景色 (RGB565形式)
// 天気ごとに異なる背景色を適用して、視覚的に分かりやすくしています。
// ------------------------------------------------------------------
#define COL_BG_CLEAR  0x0952   // 快晴: #1a2a4a
#define COL_BG_RAIN   0x0926   // 雨: #1a2535
#define COL_BG_SNOW   0x0E46   // 雪: #1c2535
#define COL_BG_THUNDER 0x08A5  // 雷: #1a1a2e
#define COL_BG_FOG    0x0904   // 霧: #1e2020
#define COL_BG_CLOUDY 0x08E4   // 曇り: #1a1e22
#define COL_BG_CLOCK  0x00A4   // 時計エリア: #0d1f3c相当

// ------------------------------------------------------------------
// タイマー間隔 (ミリ秒)
// ノンブロッキング処理のための時間間隔 Defines。
// delay() を使わず、millis() でタイミングを制御しています。
// ------------------------------------------------------------------
const unsigned long fetchInterval    = 15UL * 60UL * 1000UL;  // 天気取得間隔: 15分
const unsigned long citySwitchInterval = 15000UL;              // 都市切替間隔: 15秒
const unsigned long clockInterval     = 500UL;                 // 時計更新間隔: 0.5秒

#endif // CONFIG_H
