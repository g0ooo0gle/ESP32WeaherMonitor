/**
 * main.cpp - エントリポイント (setup / loop)
 *
 * ESP32 + ST7735S 1.8 インチ TFT 気象情報ステーション
 *
 * [機能概要]
 *   1. WiFiManager による自動 WiFi 接続（初回は AP モードでスマホから設定）
 *   2. NTP による JST 時刻表示（分単位）
 *   3. Open-Meteo API: 現在天気・週間予報・毎時予報
 *   4. WMO 天気コードの日本語テキスト＋アイコン
 *   5. 地方巡回モード: 選択地方の都市を 20 秒ごとに自動切替
 *   6. 自分の都市モード: 固定都市の週間/毎時予報を表示
 *   7. NHK ニュース画面: タイトル静止＋概要スクロール、自動ページ送り
 *   8. 物理 3 ボタン（短押しのみ）で全機能を操作
 *   9. Web 設定ページ: 自分の都市・巡回地方を変更・NVS 保存
 *
 * [ハードウェア接続 - ディスプレイ (ST7735S)]
 *   VCC → 3.3V / GND → GND
 *   CS  → GPIO 5 / RST → GPIO 4 / D/C → GPIO 2
 *   SDA → GPIO 23 (MOSI) / SCLK → GPIO 18
 *   BL  → 3.3V（バックライト常時点灯）
 *
 * [ハードウェア接続 - ボタン]
 *   SCREEN: GPIO  0 ↔ GND  （内部プルアップ使用）
 *   MODE  : GPIO 35 ↔ GND  （外付け 10kΩ → 3.3V 必要）
 *   NEXT  : GPIO 34 ↔ GND  （外付け 10kΩ → 3.3V 必要）
 *
 * [ファイル構成]
 *   config.h        ピン/レイアウト/色/タイマー定数
 *   cities.h/cpp    47 都道府県庁所在地データ
 *   weather.h/cpp   WMO 変換/背景色/アイコン/週間&毎時描画
 *   display.h/cpp   天気画面描画（差分更新対応）
 *   network.h/cpp   WiFi/NTP/天気 API/非同期取得タスク
 *   ticker.h/cpp    ニュース画面（Core 0 バックグラウンドタスク）
 *   button.h/cpp    3 ボタン制御（短押しのみ）
 *   settings.h/cpp  Web 設定ページ・NVS 保存・地方フィルタ
 *   main.cpp        setup() / loop()
 */

#include "config.h"
#include "cities.h"
#include "weather.h"
#include "display.h"
#include "network.h"
#include "ticker.h"
#include "button.h"
#include "settings.h"

// ================================================================
// ディスプレイインスタンスの実体
// ================================================================
Adafruit_ST7735      tft  = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// ================================================================
// [内部] 起動ローディング画面を描画する
//
// 画面中央に進捗メッセージと「ステップ N/total」を表示します。
// setup() の各フェーズで呼ぶことで、ユーザーに進行状況を伝えます。
// ================================================================
static void drawBootScreen(const char* msg, int step, int total)
{
  tft.fillScreen(ST77XX_BLACK);

  // タイトル
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(ST77XX_CYAN);
  int tw = u8g2.getUTF8Width("起動中");
  u8g2.setCursor((SCREEN_W - tw) / 2, 45);
  u8g2.print("起動中");

  // プログレスバー（外枠）
  const int barX = 10, barY = 55, barW = SCREEN_W - 20, barH = 8;
  tft.drawRect(barX, barY, barW, barH, ST77XX_WHITE);
  int fillW = (barW - 2) * step / total;
  if (fillW > 0) tft.fillRect(barX + 1, barY + 1, fillW, barH - 2, ST77XX_CYAN);

  // メッセージ
  u8g2.setFont(u8g2_font_b12_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  tw = u8g2.getUTF8Width(msg);
  if (tw > SCREEN_W - 4) tw = SCREEN_W - 4;
  u8g2.setCursor((SCREEN_W - tw) / 2, 85);
  u8g2.print(msg);

  // ステップ表示
  char stepBuf[16];
  snprintf(stepBuf, sizeof(stepBuf), "%d / %d", step, total);
  u8g2.setFont(u8g2_font_helvB10_tf);
  u8g2.setForegroundColor(0x8410);
  int sw = u8g2.getUTF8Width(stepBuf);
  u8g2.setCursor((SCREEN_W - sw) / 2, 105);
  u8g2.print(stepBuf);
}

// ================================================================
// setup() - 起動時に 1 回実行
//
// [起動フロー]
//   1. ディスプレイ初期化
//   2. WiFi 接続           → 起動画面 1/5
//   3. NTP 時刻同期        → 起動画面 2/5
//   4. 設定読み込み (NVS)  → 起動画面 3/5
//   5. 天気データ取得      → 起動画面 4/5
//   6. 画面表示 / タスク起動 → 起動画面 5/5
//   7. ループ開始
// ================================================================
void setup()
{
  Serial.begin(115200);
  Serial.println(F("\n[Setup] ESP32 Weather Station starting..."));

  // ---- ディスプレイ初期化 ----
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);
  u8g2.begin(tft);
  u8g2.setFontMode(1);
  u8g2.setBackgroundColor(ST77XX_BLACK);

  // ---- WiFi 接続 ----
  drawBootScreen("WiFi 接続中...", 1, 5);
  if (!setupWiFi()) {
    Serial.println(F("[Setup] WiFi 接続失敗。再起動します。"));
    ESP.restart();
  }

  // ---- NTP 時刻同期 ----
  drawBootScreen("時刻同期中...", 2, 5);
  setupNTP();

  // ---- 設定読み込み (NVS) ----
  drawBootScreen("設定を読み込み中...", 3, 5);
  loadSettings();
  setupWebServer();
  setupButtons();
  cityIndex = getFirstCityInRegion();

  // ---- 初回天気データ取得（同期・起動時のみ）----
  drawBootScreen("天気データ取得中...", 4, 5);
  updateWeather();
  updateWeeklyForecast();

  // ---- 画面表示・バックグラウンドタスク起動 ----
  drawBootScreen("完了！", 5, 5);
  delay(300);

  drawStaticElements();
  drawWeatherInfo();

  Serial.println(F("[Setup] ニュース機能初期化..."));
  setupNews();

  Serial.println(F("[Setup] 天気非同期取得タスク起動..."));
  startWeatherFetchTask();

  // 起動直後の二重取得を防ぐためタイマーをリセット
  unsigned long tNow = millis();
  lastFetchAttempt = tNow;
  lastWeeklyFetch  = tNow;
  lastHourlyFetch  = tNow;
  lastClockUpdate  = tNow;

  Serial.println(F("[Setup] 完了。loop() に移行。"));
  Serial.printf("[Setup] 空きヒープ: %u bytes\n", ESP.getFreeHeap());
}

// ================================================================
// loop() - 起動後に繰り返し実行
//
// [実行方針]
//   - ボタン処理は両画面共通で毎ループ実行
//   - 都市の切り替えはボタン操作のみ（自動切り替えなし）
//   - 天気データ更新は WEATHER 画面表示中にのみ行う
//     （NEWS 表示中は Core 0 を RSS 取得専用にして HTTP 渋滞を防ぐ。
//       天気画面へ復帰すると即時取得が走るため鮮度は保たれる）
//   - 描画は currentScreen に応じて分岐
// ================================================================
void loop()
{
  unsigned long now = millis();

  // ---- ボタン処理（毎ループ最優先）----
  updateButtons();

  // ---- Web 設定サーバー ----
  handleWebServer();

  // ---- Web 設定保存後の即時反映 ----
  if (settingsChanged) {
    settingsChanged = false;
    Serial.println(F("[Loop] 設定変更を検知: 天気取得を要求"));
    if (currentScreen == Screen::WEATHER) {
      drawWeatherInfo();
      showLoadingOverlay("設定を適用中...");
    }
    requestWeatherFetch(FETCH_CURRENT | FETCH_WEEKLY);
    lastFetchAttempt = now;
    lastWeeklyFetch  = now;
  }

  // ---- 天気データ更新完了 → 再描画 ----
  {
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
    uint8_t ready     = weatherFetchReady;
    weatherFetchReady = 0;
    portEXIT_CRITICAL(&mux);

    if (ready && currentScreen == Screen::WEATHER) {
      drawWeatherInfo();
    }
  }

  // ====================================================================
  // 画面別の描画処理
  // ====================================================================
  if (currentScreen == Screen::WEATHER)
  {
    // 1 秒ごとにチェックし、分が変化した場合のみ時計を再描画
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
  // データ更新スケジュール（WEATHER 画面のみ）
  // NEWS 画面表示中は天気の定期取得を行わない
  // ====================================================================
  if (currentScreen == Screen::WEATHER)
  {
    // 現在天気の定期更新 (15 分)
    if (now - lastFetchAttempt >= fetchInterval) {
      Serial.println(F("[Loop] 現在天気 定期更新を予約"));
      requestWeatherFetch(FETCH_CURRENT);
      lastFetchAttempt = now;
    }

    // 詳細データの定期更新
    if (currentMode == DisplayMode::SINGLE && currentSub == SubView::HOURLY) {
      if (now - lastHourlyFetch >= hourlyFetchInterval) {
        Serial.println(F("[Loop] 毎時天気 定期更新を予約"));
        requestWeatherFetch(FETCH_HOURLY);
        lastHourlyFetch = now;
      }
    } else {
      if (now - lastWeeklyFetch >= weeklyFetchInterval) {
        Serial.println(F("[Loop] 週間天気 定期更新を予約"));
        requestWeatherFetch(FETCH_WEEKLY);
        lastWeeklyFetch = now;
      }
    }
  }
}
