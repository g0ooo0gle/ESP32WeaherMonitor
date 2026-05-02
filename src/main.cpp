/**
 * ESP32 Weather Station
 * ESP32 + ST7735S 1.8inch Display Project
 *
 * [機能]
 * 1. WiFiManagerによる自動接続
 * 2. NTPによる秒単位の正確な時刻表示
 * 3. Open-Meteo APIによる日本全国主要都市の巡回
 * 4. 全天気コード(WMO)の完全日本語化
 * 5. WMO天気コードに応じたアイコン表示（プリミティブ描画）
 * 6. 気象状況に応じた背景色の動的変更
 * 7. NHKニュースRSSをスクロール表示するティッカー機能
 * 8. 物理ボタンによるモード切替・都市切替
 *
 * [接続メモ]
 * VCC    -> 3.3V
 * GND    -> GND
 * CS     -> GPIO 5
 * RESET  -> GPIO 4
 * D/C(A0)-> GPIO 2
 * SDA    -> GPIO 23
 * SCLK   -> GPIO 18
 * BL(LED)-> 3.3V (バックライト)
 *
 * [ボタン接続]
 * モード切替ボタン: GPIO 0  ← → GND
 * 都市切替ボタン  : GPIO 35 ← → GND
 * （両ボタンとも内部プルアップを使うので外付け抵抗は不要）
 *
 * ============================================================
 * ファイル構成（機能別に分離）
 * ============================================================
 * config.h      : ピン配置、レイアウト定数、色、インターバルの全定義
 * cities.h/cpp  : 都市データ（10都市のリスト）
 * weather.h/cpp : WMOコード変換、背景色判定、天気アイコン描画
 * display.h/cpp : すべての描画処理（時計・都市名・気温・天気説明）
 * network.h/cpp : WiFi接続、NTP同期、API通信
 * ticker.h/cpp  : NHKニュースRSS取得＆スクロール描画
 * button.h/cpp  : 物理ボタン制御（デバウンス・モード切替）
 * main.cpp      : setup() と loop() のみ（ここ）← エントリポイント
 * ============================================================
 */

// ヘッダの読み込み順は重要（config.h が一番先）
#include "config.h"
#include "cities.h"
#include "weather.h"
#include "display.h"
#include "network.h"
#include "ticker.h"    //  ティッカー機能
#include "button.h"    //  ボタン制御

// ディスプレイインスタンス（定義場所）
// tft  : ST7735S ディスプレイドライバ
// u8g2 : Adafruit_GFX 用の日本語フォントラッパー
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// ============================================================
// setup() - 起動時に一度だけ実行
//
// 全体の流れ:
//   1. シリアル通信開始
//   2. ディスプレイ初期化
//   3. WiFi接続
//   4. 時刻同期
//   5. 静的レイアウト描画
//   6. ボタン初期化
//   7. 初期天気データ取得＆全エリア描画
//   8. ティッカー初期化（RSSを初回取得）
//   9. loop() へ
// ============================================================
void setup()
{
  // シリアルモニタ用（デバッグ出力用）
  Serial.begin(115200);
  Serial.println(F("\n[Setup] ESP32 Weather Station starting..."));

  // --- ディスプレイ初期化 ---
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);             // 縦向き (128x160)
  tft.fillScreen(ST77XX_BLACK);

  // --- u8g2 日本語フォントラッパー初期化 ---
  // u8g2 は Adafruit_GFX の上に日本語フォントを重ねるラッパーです。
  // begin(tft) で tft インスタンスと紐付けます。
  u8g2.begin(tft);

  // --- WiFi接続（未設定時は "ESP32_Weather_Config" というAPになる） ---
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(0, 80);
  u8g2.print("WiFi setup...");
  Serial.println(F("[Setup] Starting WiFiManager..."));

  if (!setupWiFi())
  {
    Serial.println(F("[Setup] WiFi接続に失敗。再起動します。"));
    ESP.restart();
  }
  Serial.println(F("[Setup] WiFi connected."));

  tft.fillScreen(ST77XX_BLACK);
  u8g2.setCursor(0, 80);
  u8g2.print("WiFi接続完了！");
  delay(500);   // 接続完了メッセージを一瞬見せる

  // --- 時刻同期（NTP） ---
  tft.fillScreen(ST77XX_BLACK);
  u8g2.setCursor(0, 80);
  u8g2.print("時刻同期中...");
  Serial.println(F("[Setup] Syncing time via NTP..."));

  setupNTP();   // NICT と Google の NTP サーバーに接続
  Serial.println(F("[Setup] NTP configured."));

  // --- 静的レイアウトを一度だけ描画（仕切り線など） ---
  drawStaticElements();

  // ---  ボタン初期化 ---
  // GPIO ピンを INPUT_PULLUP に設定します。
  // WiFi 接続後にやると GPIO 0 の ENボタン干渉を避けやすいです。
  setupButtons();

  // --- タイマー初期化 ---
  // loop() の millis 比較で使うタイマー変数を初期化します。
  // lastFetchAttempt を「今からfetchInterval秒前」にすることで
  // 起動直後に最初の天気取得がすぐ実行されます。
  lastFetchAttempt = millis() - fetchInterval;
  lastCitySwitch   = millis();
  lastClockUpdate  = millis();

  // --- 初回天気データ取得 & 全エリア一筆描画 ---
  Serial.println(F("[Setup] 初期天気データ取得中..."));
  updateWeather();
  drawWeatherInfo();

  // ---  ティッカー初期化（NHKニュースを初回取得）---
  // WiFi接続・NTP同期が完了した後に呼ぶ必要があります。
  // RSSの取得には数秒かかることがあるため、setupの最後に置いています。
  Serial.println(F("[Setup] ニューステッカー初期化中..."));
  setupTicker();

  Serial.println(F("[Setup] setup完了。loop() に移行。"));
}

// ============================================================
// loop() - 起動後にループ実行
//
// ノンブロッキング設計のため delay() は使いません。
// millis() でタイマーを管理し、4つのタスクを独立して処理します。
//
// [タスク一覧]
//   ① ボタン確認  : 毎ループ（デバウンスは button.cpp 内で管理）
//   ② 時計更新    : 0.5秒ごとにチェック（差分があった時だけ描画）
//   ③ ティッカー  : 40msごとにスクロール描画 / 5分ごとにRSS更新
//   ④ 都市切替    : 15秒ごとにループ（天気モード時のみ）
//   ⑤ 天気更新    : 15分ごとにAPIから再取得
//
// [モード別の動作]
//   DisplayMode::WEATHER : ②時計 + ④都市切替 + ⑤天気更新 を実行
//   DisplayMode::TICKER  : ①ボタン + ③ティッカー のみ実行（天気描画は止める）
// ============================================================
void loop()
{
  unsigned long now = millis();

  // ============================================================
  // ① ボタン確認（毎ループ実行）
  // デバウンス処理は updateButtons() の内部で行っています。
  // ============================================================
  updateButtons();

  // ============================================================
  // 表示モードに応じて処理を分岐
  // ============================================================

  if (currentMode == DisplayMode::WEATHER)
  {
    // ----------------------------------------------------------
    // 天気表示モード
    // ----------------------------------------------------------

    // ② 時計更新（0.5秒ごとにチェック・差分があった時だけ描画）
    // 差分描画のため高頻度チェックしてもちらつきや負荷は発生しない
    if (now - lastClockUpdate >= clockInterval)
    {
      drawClock();
      lastClockUpdate = now;
    }

    // ④ 都市切替（15秒ごと）
    if (now - lastCitySwitch >= citySwitchInterval)
    {
      // 都市インデックスを次に進める（最後→戻ったら最初へ）
      cityIndex = (cityIndex + 1) % cityCount;
      Serial.printf("[Loop] 都市切替 → %s\n", cities[cityIndex].name);

      // 新しい都市の天気を即座に取得して全エリアを更新
      updateWeather();
      drawWeatherInfo();
      prevCityIndex  = cityIndex;
      lastCitySwitch = now;
    }

    // ⑤ 定期天気更新（15分ごと）
    // データが変化した場合のみ再描画（ちらつき防止）
    if (now - lastFetchAttempt >= fetchInterval)
    {
      Serial.println(F("[Loop] 定期天気更新..."));
      bool changed = updateWeather();
      if (changed)
      {
        drawWeatherInfo();   // 変化があった場合のみ再描画
      }
      // 成否にかかわらずタイマーをリセット（レート制限対策）
      lastFetchAttempt = now;
    }

    // ③ ティッカースクロール（天気モードでも画面下部に表示）
    // TICKER_AREA_Y（=140px）より下のエリアを使い、
    // 天気情報（0〜139px）と重ならないよう設計されています。
    updateTicker();
  }
  else if (currentMode == DisplayMode::TICKER)
  {
    // ----------------------------------------------------------
    // ティッカー全画面モード（将来拡張用）
    // 現在は天気モードのティッカーと同じ updateTicker() を呼ぶだけです。
    // 将来的にはここでニュースを大きなフォントで全画面表示する
    // 専用描画関数を呼ぶことを想定しています。
    // ----------------------------------------------------------
    updateTicker();
  }

  // delay は使わない（ノンブロッキング設計）
}
