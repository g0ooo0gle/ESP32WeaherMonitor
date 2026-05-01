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
 * ============================================================
 * リファクタリング後の構成（ファイルを機能別に分離済み）
 * ============================================================
 * config.h   : ピン配置、レイアウト定数、色、インターバルの全定義
 * cities.h/pp: 都市データ（10都市のリスト）
 * weather.h/pp: WMOコード変換、背景色判定、天気アイコン描画
 * display.h/pp: すべての描画処理（時計・都市名・気温・天気説明）
 * network.h/pp: WiFi接続、NTP同期、API通信
 * main.cpp   : setup() と loop() のみ（ここ） ← これがエントリポイント
 * ============================================================
 */

// ヘッダの読み込み順は important（config.h が一番先）
#include "config.h"
#include "cities.h"
#include "weather.h"
#include "display.h"
#include "network.h"

// ディスプレイインスタンス（定義場所）
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// ============================================================
// setup() - 起動時に一度だけ実行
// 全体の流れ:
//   1. シリアル通信開始
//   2. ディスプレイ初期化
//   3. WiFi接続
//   4. 時刻同期
//   5. 初期天気データ取得
//   6. 全エリア描画 → loop() へ
// ============================================================
void setup()
{
  // シリアルモニタ用（デバッグ出力用）
  Serial.begin(115200);
  Serial.println(F("\n[Setup] ESP32 Weather Station starting..."));

  // --- ディスプレイ初期化 ---
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0); // 縦向き (128x160)
  tft.fillScreen(ST77XX_BLACK);

  // --- WiFi接続（未設定時は "ESP32_Weather_Config" というAPになる） ---
  // setupWiFi() は network.cpp で実装済み
  // 保存された設定がない場合はスマホからSSID/パスワードを設定可能）
  u8g2.begin(tft);
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
  delay(500); // 接続完了メッセージを一瞬見せる

  // --- 時刻同期（NTP） ---
  tft.fillScreen(ST77XX_BLACK);
  u8g2.setCursor(0, 80);
  u8g2.print("時刻同期中...");
  Serial.println(F("[Setup] Syncing time via NTP..."));

  // setupNTP() は network.cpp で実装済み
  // NICT と Google のNTPサーバーからJST（UTC+9）を取得
  setupNTP();
  Serial.println(F("[Setup] NTP configured."));

  // --- 静的レイアウトを一度だけ描画 ---
  // setup() でだけ呼ばれる処理を display.cpp に実装
  drawStaticElements();

  // --- タイマー初期化 ---
  // loop() の millis 比較で使うタイマー変数を初期化
  // fetchAttempt を「今からfetchInterval分前」にする = 初回即時実行のため
  lastFetchAttempt = millis() - fetchInterval;
  lastCitySwitch = millis();
  lastClockUpdate = millis();

  // --- 初回天気データ取得 & 全エリア一筆描画 ---
  Serial.println(F("[Setup] 初期天気データ取得中..."));
  updateWeather();

  // drawWeatherInfo() は display.cpp にて実装済み
  // 背景・都市名・気温・天気説明をすべて一筆描画
  drawWeatherInfo();

  Serial.println(F("[Setup] setup完了。loop() に移行。"));
}

// ============================================================
// loop() - 起動後にループ実行
// 非同期（ノンブロッキング）処理のため、delay() は使いません。
// millis() の値でタイミングを管理しています。
//
// 処理の流れ（3つのタスクを独立に回す）:
//   ① 時計更新:   毎1秒（差分あった時のみ）
//   ② 都市切替:   15秒ごとにループ
//   ③ 天気更新:   15分ごとにAPI取得
// ============================================================
void loop()
{
  unsigned long now = millis();

  // ① 時計更新（0.5秒ごとにチェック・差分があった時だけ描画）
  // 差分描画のため高頻度チェックしてもちらつきや負荷は発生しない
  if (now - lastClockUpdate >= clockInterval)
  {
    drawClock();
    lastClockUpdate = now;
  }

  // ② 都市切替（15秒ごと）
  if (now - lastCitySwitch >= citySwitchInterval)
  {
    // 都市インデックスを次に進める（最後→戻ったら最初へ）
    cityIndex = (cityIndex + 1) % cityCount;
    Serial.printf("[Loop] 都市切替 -> %s\n", cities[cityIndex].name);

    // 新しい都市の天気を即座に取得
    updateWeather();
    
    // 背景・都市名・気温・天気をすべて一括で更新
    drawWeatherInfo();
    prevCityIndex = cityIndex;
    lastCitySwitch = now;
  }

  // ③ 定期天気更新（15分ごと）
  // データが変化した場合のみ再描画するので、都市切替とは独立して動作する
  if (now - lastFetchAttempt >= fetchInterval)
  {
    Serial.println(F("[Loop] 定期天気更新..."));
    bool changed = updateWeather();
    if (changed)
    {
      drawWeatherInfo(); // 変化があった場合のみ再描画（ちらつき防止）
    }
    lastFetchAttempt = now; // 成否にかかわらずタイマーをリセット（レート制限対策）
  }
  
  // ここで delay は使わない（ノンブロッキング設計）
}
