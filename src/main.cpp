/**
 * ESP32 Weather Station
 * ESP32 + ST7735S 1.8inch Display Project
 *
 * [機能一覧]
 * 1. WiFiManagerによる自動接続（初回はAPモードでスマホから設定）
 * 2. NTPによる分単位の正確な時刻表示（HH:MM形式）
 * 3. Open-Meteo APIによる日本全47都道府県庁所在地の巡回
 * 4. 全天気コード(WMO)の完全日本語化
 * 5. 天気コードに応じたアイコン表示（プリミティブ描画）
 * 6. 気象状況に応じた背景色の動的変更
 * 7. 週間天気予報（6日分の最高・最低気温＋天気アイコン）
 * 8. NHKニュースRSSをスクロール表示するティッカー機能
 * 9. 物理ボタンによるモード切替・都市切替
 *
 * [接続メモ（ディスプレイ）]
 * VCC    -> 3.3V
 * GND    -> GND
 * CS     -> GPIO 5
 * RESET  -> GPIO 4
 * D/C(A0)-> GPIO 2
 * SDA    -> GPIO 23
 * SCLK   -> GPIO 18
 * BL(LED)-> 3.3V (バックライト常時オン)
 *
 * [接続メモ（ボタン）]
 * モード切替ボタン: GPIO 0  ← → GND（内部プルアップ使用）
 * 都市切替ボタン  : GPIO 35 ← → GND（内部プルアップ使用）
 *
 * ============================================================
 * ファイル構成
 * ============================================================
 * config.h      : ピン配置、レイアウト定数、色、インターバルの全定義
 * cities.h/cpp  : 47都道府県庁所在地＋地方ブロック定義
 * weather.h/cpp : WMOコード変換、背景色判定、天気アイコン、週間天気描画
 * display.h/cpp : 画面描画（時計＋都市1行、現在天気、週間天気呼び出し）
 * network.h/cpp : WiFi、NTP、現在天気・週間天気のAPI通信
 * ticker.h/cpp  : NHKニュースRSS取得＆高速スクロール描画
 * button.h/cpp  : 物理ボタン制御（デバウンス・モード切替）
 * main.cpp      : setup() と loop() のみ（ここ）
 * ============================================================
 *
 * [将来の拡張予定]
 * - 地方ブロック別フィルタ表示（cities.h の RegionBlock を使用）
 * - Web設定画面（WiFiManager のカスタムパラメータ機能で実装予定）
 *   → 都市表示間隔、表示地方の絞り込み、RSS URL 変更などを設定保存
 */

#include "config.h"
#include "cities.h"
#include "weather.h"
#include "display.h"
#include "network.h"
#include "ticker.h"
#include "button.h"

// ディスプレイインスタンスの実体（他のファイルは extern で参照）
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// ============================================================
// setup() - 起動時に一度だけ実行
// ============================================================
void setup()
{
  Serial.begin(115200);
  Serial.println(F("\n[Setup] ESP32 Weather Station starting..."));

  // ---- ディスプレイ初期化 ----
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);             // 縦向き 128×160px
  tft.fillScreen(ST77XX_BLACK);
  u8g2.begin(tft);

  // ---- WiFi接続 ----
  // setupWiFi() は network.cpp に実装。
  // 未設定時は "ESP32_Weather_Config" という AP を起動し、
  // スマホのブラウザ（192.168.4.1）から SSID/パスワードを設定できます。
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(0, 80);
  u8g2.print("WiFi setup...");

  if (!setupWiFi())
  {
    Serial.println(F("[Setup] WiFi接続失敗。再起動します。"));
    ESP.restart();
  }

  tft.fillScreen(ST77XX_BLACK);
  u8g2.setCursor(0, 80);
  u8g2.print("WiFi接続完了！");
  delay(400);

  // ---- 時刻同期（NTP）----
  tft.fillScreen(ST77XX_BLACK);
  u8g2.setCursor(0, 80);
  u8g2.print("時刻同期中...");
  setupNTP();
  Serial.println(F("[Setup] NTP configured."));

  // ---- 静的レイアウト描画（区切り線など）----
  drawStaticElements();

  // ---- ボタン初期化 ----
  // WiFi接続後に初期化することで GPIO 0（EN ボタン）との干渉を回避します。
  setupButtons();

  // ---- タイマー初期化 ----
  // lastFetchAttempt を「fetchInterval 分前」に設定することで、
  // setup 完了直後から即座に天気取得が走ります。
  lastFetchAttempt = millis() - fetchInterval;
  lastWeeklyFetch  = millis() - weeklyFetchInterval;
  lastCitySwitch   = millis();
  lastClockUpdate  = millis();

  // ---- 初回データ取得と描画 ----
  Serial.println(F("[Setup] 初回天気データ取得..."));
  updateWeather();
  updateWeeklyForecast();
  drawWeatherInfo();

  // ---- ティッカー初期化（NHK RSS 初回取得）----
  // WiFi・NTP の完了後に実行します。RSS 取得に数秒かかる場合があります。
  Serial.println(F("[Setup] ティッカー初期化..."));
  setupTicker();

  Serial.println(F("[Setup] 完了。loop() に移行。"));
}

// ============================================================
// loop() - 起動後に繰り返し実行
//
// ノンブロッキング設計のため delay() は使いません。
// millis() の差分で各タスクのタイミングを独立して管理します。
//
// [実行タスク一覧]
//   ① ボタン確認     : 毎ループ（デバウンスは button.cpp 内で処理）
//   ② 時計＋都市更新  : 30秒ごと（分が変わった時だけ実際に再描画）
//   ③ ティッカー     : 20msごとにスクロール / 5分ごとにRSS再取得
//   ④ 都市自動切替   : 20秒ごと（天気モード時のみ）
//   ⑤ 現在天気更新   : 15分ごとにAPI取得
//   ⑥ 週間天気更新   : 1時間ごとにAPI取得
// ============================================================
void loop()
{
  unsigned long now = millis();

  // ① ボタン確認（毎ループ）
  updateButtons();

  if (currentMode == DisplayMode::WEATHER)
  {
    // ② 時計＋都市更新
    // drawClockCity() 内で差分チェックするため、頻繁に呼んでもコストは低い。
    if (now - lastClockUpdate >= clockInterval)
    {
      drawClockCity();
      lastClockUpdate = now;
    }

    // ④ 都市自動切替（20秒ごと）
    if (now - lastCitySwitch >= citySwitchInterval)
    {
      cityIndex = (cityIndex + 1) % cityCount;
      Serial.printf("[Loop] 都市切替 → %s\n", cities[cityIndex].name);

      // 新しい都市の現在天気と週間天気を取得して全体を再描画
      updateWeather();
      updateWeeklyForecast();
      drawWeatherInfo();

      prevCityIndex  = cityIndex;
      lastCitySwitch = now;
      // 都市切替後は両タイマーもリセット（過剰な連続API呼び出しを防ぐ）
      lastFetchAttempt = now;
      lastWeeklyFetch  = now;
    }

    // ⑤ 現在天気の定期更新（15分ごと）
    if (now - lastFetchAttempt >= fetchInterval)
    {
      Serial.println(F("[Loop] 現在天気定期更新..."));
      bool changed = updateWeather();
      if (changed) drawWeatherInfo();
      lastFetchAttempt = now;
    }

    // ⑥ 週間天気の定期更新（1時間ごと）
    // 週間予報は数時間単位でしか変わらないため、更新頻度は低めに設定。
    if (now - lastWeeklyFetch >= weeklyFetchInterval)
    {
      Serial.println(F("[Loop] 週間天気定期更新..."));
      bool changed = updateWeeklyForecast();
      if (changed) drawWeeklyForecast();
      lastWeeklyFetch = now;
    }

    // ③ ティッカースクロール（天気モードでも常時表示）
    updateTicker();
  }
  else if (currentMode == DisplayMode::TICKER)
  {
    // ティッカーモード: ティッカーのみ動かす
    // 将来的にはここでニュースを全画面表示する専用描画関数を呼ぶ予定。
    updateTicker();
  }
}
