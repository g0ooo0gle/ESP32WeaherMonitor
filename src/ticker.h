/**
 * ESP32 Weather Station - ニューステキストティッカー ヘッダ
 *
 * [スクロール改善点]
 * 旧: 毎フレームでエリア全体を fillRect → 文字描画 → ちらつき・描画遅延
 * 新: エリアを 2分割して交互に描画する「部分更新方式」を採用
 *     → fillRect の範囲を最小化して描画時間を短縮
 *     → 更新間隔を 20ms に短縮（旧 40ms）、スピードを 3px/frame に増速
 *     → 結果: 約 150px/秒 のスムーズなスクロール
 *
 * [RSSについて]
 * NHK RSS: https://www3.nhk.or.jp/rss/news/cat0.xml
 * cat0=主要, cat1=社会, cat2=科学, cat3=政治, cat4=経済
 */

#pragma once

#include "config.h"

// ----------------------------------------------------------------
// スクロール速度と更新間隔
// ----------------------------------------------------------------

// 1フレームあたりの移動量（px）
// 3px × 20ms = 150px/秒（旧: 2px × 40ms = 50px/秒の3倍）
#define TICKER_SCROLL_SPEED     3

// スクロール更新間隔（ミリ秒）
// 20ms = 約50fps。ST7735S の SPI 速度で安定して描画できる限界に近い値。
#define TICKER_UPDATE_INTERVAL  20

// RSS再取得間隔（5分）
#define TICKER_FETCH_INTERVAL   (5UL * 60UL * 1000UL)

// ----------------------------------------------------------------
// ティッカーエリア定数（config.h の値と一致させる）
// ----------------------------------------------------------------
#define TICKER_AREA_Y    AREA_TICKER_Y   // 140
#define TICKER_AREA_H    AREA_TICKER_H   // 20
#define TICKER_AREA_W    SCREEN_W        // 128

// 色
#define TICKER_BG_COLOR    ST77XX_BLACK
#define TICKER_TEXT_COLOR  ST77XX_YELLOW

// ----------------------------------------------------------------
// 関数宣言
// ----------------------------------------------------------------

/** setup() の最後で1回だけ呼ぶ */
void setupTicker();

/**
 * loop() から毎回呼ぶ（内部でタイマー管理）
 * スクロール描画と定期RSS更新を行います。
 */
void updateTicker();

/** RSS を即座に取得・更新します（setupTicker / updateTicker から内部呼び出し）*/
void fetchNews();

// ----------------------------------------------------------------
// 状態変数（外部参照用）
// ----------------------------------------------------------------
extern String tickerText;    // 現在表示中のテキスト
extern int    tickerScrollX; // スクロール位置（px）
