/**
 * ESP32 Weather Station
 * ESP32 + ST7735S 1.8inch Display Project
 *
 * [今回の変更点 (画面分離)]
 *   - WEATHER画面と NEWS画面を完全分離
 *   - MODEボタン短押しで画面切替
 *   - 各画面はそれぞれ独立して描画され、互いに干渉しません
 *   - 天気更新・RSS 取得は両画面で並行動作 (Core 0/1 両方使用)
 *
 * [機能一覧]
 *   1. WiFiManager 自動接続 (初回はAPモードでスマホ設定)
 *   2. NTP分単位の時刻表示
 *   3. Open-Meteo API: 現在天気/週間/毎時を取得
 *   4. WMO天気コードの日本語化＋アイコン
 *   5. 地方巡回モード: 選択した地方の都市を20秒ごとに自動切替
 *   6. 自分の都市モード: 固定都市の週間/毎時予報を表示
 *   7. NHKニュース画面: 1見出し全画面ページ表示、5秒で次へ
 *   8. 物理ボタン3個 (短押しのみ) で全機能を操作
 *   9. Web設定ページ: 自分の都市・巡回する地方を変更・保存
 *
 * [接続メモ (ディスプレイ)]
 *   VCC -> 3.3V / GND -> GND
 *   CS  -> GPIO 5 / RESET -> GPIO 4 / D/C(A0) -> GPIO 2
 *   SDA -> GPIO 23 / SCLK -> GPIO 18
 *   BL  -> 3.3V (バックライト常時オン)
 *
 * [接続メモ (ボタン)] 全て短押しのみ・長押しなし
 *   SCREEN: GPIO  0 ← → GND  (天気 ⇄ ニュース 切替)
 *   MODE  : GPIO 35 ← → GND  (地方巡回 ⇄ 自分の都市 切替)
 *   NEXT  : GPIO 34 ← → GND  (次の都市 / 週間⇄毎時 / 次の見出し)
 *           ※ GPIO 34 は内部プルアップ非対応。外付け 10kΩ → 3.3V 必要。
 *
 * [ファイル構成]
 *   config.h        : ピン/レイアウト/色/インターバル定義
 *   cities.h/cpp    : 47都道府県庁所在地データ
 *   weather.h/cpp   : WMO変換/背景色/アイコン/週間&毎時描画
 *   display.h/cpp   : 天気画面の描画
 *   network.h/cpp   : WiFi/NTP/天気API
 *   ticker.h/cpp    : ニュース画面 (描画＋Core0タスク)
 *   button.h/cpp    : ボタン処理 (3ボタン・短押しのみ)
 *   settings.h/cpp  : Web設定ページ・NVS保存・地方フィルタ
 *   main.cpp        : setup() と loop()
 */

#include "config.h"
#include "cities.h"
#include "weather.h"
#include "display.h"
#include "network.h"
#include "ticker.h"
#include "button.h"
#include "settings.h"

// ディスプレイインスタンスの実体
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// ============================================================
// setup() - 起動時に1回実行
// ============================================================
void setup()
{
  Serial.begin(115200);
  Serial.println(F("\n[Setup] ESP32 Weather Station starting..."));

  // ---- ディスプレイ初期化 ----
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);
  u8g2.begin(tft);
  u8g2.setFontMode(1);   // デフォルトで透過モード

  // ---- WiFi接続 ----
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setBackgroundColor(ST77XX_BLACK);
  u8g2.setCursor(0, 80);
  u8g2.print("WiFi setup...");

  if (!setupWiFi()) {
    Serial.println(F("[Setup] WiFi接続失敗。再起動します。"));
    ESP.restart();
  }

  tft.fillScreen(ST77XX_BLACK);
  u8g2.setCursor(0, 80);
  u8g2.print("WiFi接続完了!");
  delay(400);

  // ---- 時刻同期 ----
  tft.fillScreen(ST77XX_BLACK);
  u8g2.setCursor(0, 80);
  u8g2.print("時刻同期中...");
  setupNTP();
  Serial.println(F("[Setup] NTP configured."));

  // ---- 設定読み込み (NVS) ----
  loadSettings();

  // ---- Web 設定サーバー起動 ----
  setupWebServer();

  // ---- 静的レイアウト ----
  drawStaticElements();

  // ---- ボタン初期化 ----
  setupButtons();

  // ---- 起動時の都市を地方フィルタに合わせて初期化 ----
  cityIndex = getFirstCityInRegion();

  // ---- 初回データ取得と描画 (同期・起動時のみ) ----
  Serial.println(F("[Setup] 初回天気データ取得..."));
  updateWeather();
  updateWeeklyForecast();
  drawWeatherInfo();

  // ---- ニュース機能初期化 (Core 0 タスク起動) ----
  Serial.println(F("[Setup] ニュース機能初期化..."));
  setupNews();

  // ---- 天気非同期取得タスク起動 (Core 0) ----
  Serial.println(F("[Setup] 天気非同期取得タスク起動..."));
  startWeatherFetchTask();

  // タイマーを現在時刻から開始（起動直後の二重取得を防ぐ）
  {
    unsigned long tNow = millis();
    lastFetchAttempt = tNow;
    lastWeeklyFetch  = tNow;
    lastHourlyFetch  = tNow;
    lastCitySwitch   = tNow;
    lastClockUpdate  = tNow;
  }

  Serial.println(F("[Setup] 完了。loop() に移行。"));
  Serial.printf("[Setup] 空きヒープ: %u bytes\n", ESP.getFreeHeap());
}

// ============================================================
// loop() - 起動後に繰り返し実行
//
// [実行方針]
//   ・ボタン処理は両画面共通で毎ループ実行
//   ・天気データの更新は両画面とも継続 (画面に出してなくても裏で更新)
//   ・描画は currentScreen に応じて分岐
//
// [画面別の処理]
//   ◆ WEATHER画面
//     - 時計更新 (30秒ごとに描画)
//     - 都市自動切替 (全国モード時のみ、20秒ごと)
//     - 現在天気更新 (15分ごと)
//     - 週間/毎時の更新 (1時間 / 30分)
//
//   ◆ NEWS画面
//     - 自動ページ切替 (5秒ごとに次の見出し)
//     - (天気の更新タイマーは裏で動き続けます。
//        画面復帰時に最新データが表示されます)
// ============================================================
void loop()
{
  unsigned long now = millis();

  // ---- ボタン処理 (両画面共通・毎回優先実行) ----
  updateButtons();

  // ---- Web 設定サーバー ----
  handleWebServer();

  // ---- Web 設定保存後の即時反映 ----
  if (settingsChanged) {
    settingsChanged = false;
    Serial.println(F("[Loop] 設定変更検知: 非同期天気取得を要求..."));
    if (currentScreen == Screen::WEATHER) {
      drawWeatherInfo();
      showLoadingOverlay("設定を適用中...");
    }
    requestWeatherFetch(FETCH_CURRENT | FETCH_WEEKLY);
    lastFetchAttempt = now;
    lastWeeklyFetch  = now;
  }

  // ---- 天気データ更新完了 → 再描画 ----
  // Core 0 タスクが完了フラグを立てたら天気画面を更新する。
  // ローディングオーバーレイも drawWeatherInfo() の全画面再描画で自然に消える。
  uint8_t ready = weatherFetchReady;
  if (ready) {
    weatherFetchReady = 0;
    if (currentScreen == Screen::WEATHER) {
      drawWeatherInfo();
    }
  }

  // ====================================================================
  // 画面ごとの描画処理
  // ====================================================================
  if (currentScreen == Screen::WEATHER)
  {
    // 時計更新: 1秒ごとにチェック、差分検出により分変化時のみ実際に描画
    if (now - lastClockUpdate >= clockInterval) {
      drawClockCity();
      lastClockUpdate = now;
    }
  }
  else  // Screen::NEWS
  {
    updateNewsAutoPaging();
  }

  // ====================================================================
  // データ更新スケジュール（Core 0 へ非同期委譲）
  //
  // [重要] NEWS 画面表示中は天気の定期取得を一切行わない。
  //   - Core 0 の HTTP 処理を RSS 取得専用にすることで渋滞を解消
  //   - WEATHER 画面復帰時に requestWeatherFetch() が呼ばれるため鮮度は保たれる
  //   - タイマー変数はここでは更新しないため、復帰後に時間が来れば自動取得される
  // ====================================================================
  if (currentScreen == Screen::WEATHER)
  {
    // 全国モード: 20秒ごとに次の都市へ切替し、取得を予約
    if (currentMode == DisplayMode::ALL_CITIES) {
      if (now - lastCitySwitch >= citySwitchInterval) {
        cityIndex = getNextCityInRegion(cityIndex);
        Serial.printf("[Loop] 都市切替 → %s\n", cities[cityIndex].name);
        drawClockCity();
        prevCityIndex    = cityIndex;
        lastCitySwitch   = now;
        lastFetchAttempt = now;
        lastWeeklyFetch  = now;
        requestWeatherFetch(FETCH_CURRENT | FETCH_WEEKLY);
      }
    }

    // 現在天気の定期更新 (15分)
    if (now - lastFetchAttempt >= fetchInterval) {
      Serial.println(F("[Loop] 現在天気定期更新を予約..."));
      requestWeatherFetch(FETCH_CURRENT);
      lastFetchAttempt = now;
    }

    // 詳細データの定期更新
    if (currentMode == DisplayMode::SINGLE && currentSub == SubView::HOURLY) {
      if (now - lastHourlyFetch >= hourlyFetchInterval) {
        Serial.println(F("[Loop] 毎時天気定期更新を予約..."));
        requestWeatherFetch(FETCH_HOURLY);
        lastHourlyFetch = now;
      }
    } else {
      if (now - lastWeeklyFetch >= weeklyFetchInterval) {
        Serial.println(F("[Loop] 週間天気定期更新を予約..."));
        requestWeatherFetch(FETCH_WEEKLY);
        lastWeeklyFetch = now;
      }
    }
  }
}
