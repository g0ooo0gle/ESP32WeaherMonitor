/**
 * ESP32 Weather Station - 物理ボタン制御 実装
 *
 * [このファイルで実装すること]
 * 1. setupButtons()  : GPIO ピンを INPUT_PULLUP で初期化
 * 2. updateButtons() : 2つのボタンを毎フレームチェックし、
 *    - BTN_MODE_PIN が押された → 表示モードを切り替える（天気↔ティッカー）
 *    - BTN_CITY_PIN が押された → 次の都市に手動で切り替える
 *
 * [チャタリング除去の仕組み]
 * ボタンは「HIGH→LOW に変わった瞬間（押し始め）」を1回だけ検出します。
 * 前回の状態（prevStateXxx）と今回の状態を比較して「立ち下がりエッジ」を検出。
 * さらに lastPressXxx から BTN_DEBOUNCE_MS(50ms) 経過していないと無視します。
 * これで同じボタン押しが2回以上登録される誤動作を防ぎます。
 */

#include "button.h"
#include "display.h"   // cityIndex, drawWeatherInfo(), drawStaticElements() を使うため
#include "network.h"   // updateWeather() を使うため
#include "ticker.h"    // updateTicker(), tickerScrollX を使うため
#include "cities.h"    // cityCount を使うため

// ================================================================
// 現在の表示モードの実体定義（button.h で extern 宣言済み）
// デフォルトは通常の天気表示（MODE_WEATHER）です。
// ================================================================
DisplayMode currentMode = DisplayMode::WEATHER;

// ================================================================
// チャタリング除去用の内部状態変数（このファイルだけで使う）
// ================================================================
static int  prevModeBtn  = HIGH;   // 前フレームのモードボタン状態（初期値 = 押されていない）
static int  prevCityBtn  = HIGH;   // 前フレームの都市ボタン状態
static unsigned long lastPressModeBtn = 0;   // 最後にモードボタンが反応した時刻
static unsigned long lastPressCityBtn = 0;   // 最後に都市ボタンが反応した時刻

// ================================================================
// ボタンの GPIO ピンを初期化
// INPUT_PULLUP = ボタン未押下時は HIGH（プルアップ抵抗内蔵で常に 3.3V）
//               ボタン押下時は LOW（ボタンが GND に接続するため電圧が下がる）
// ================================================================
void setupButtons()
{
  // 両方のボタンをプルアップ付き入力モードで初期化
  pinMode(BTN_MODE_PIN, INPUT_PULLUP);
  pinMode(BTN_CITY_PIN, INPUT_PULLUP);

  Serial.printf("[Button] ボタン初期化完了: MODE=GPIO%d, CITY=GPIO%d\n",
                BTN_MODE_PIN, BTN_CITY_PIN);
}

// ================================================================
// ボタン状態の更新（loop() から毎回呼ぶ）
// ================================================================
void updateButtons()
{
  unsigned long now = millis();

  // -------------------------------------------------------------------
  // ① モード切替ボタン（BTN_MODE_PIN）のチェック
  //
  // 動作: 押すたびに  WEATHER → TICKER → WEATHER → ... とトグルします。
  // -------------------------------------------------------------------
  int modeBtnState = digitalRead(BTN_MODE_PIN);   // 現在のピン状態を読む

  // 「前回 HIGH（未押下）で今回 LOW（押下）」 = 立ち下がりエッジを検出
  if (prevModeBtn == HIGH && modeBtnState == LOW)
  {
    // チャタリング除去: 前回反応から BTN_DEBOUNCE_MS(50ms) 以上経っていれば有効
    if (now - lastPressModeBtn >= BTN_DEBOUNCE_MS)
    {
      lastPressModeBtn = now;   // 反応時刻を更新

      // 現在のモードに応じて次のモードへ切り替え
      if (currentMode == DisplayMode::WEATHER)
      {
        currentMode = DisplayMode::TICKER;
        Serial.println(F("[Button] モード切替 → TICKER"));

        // ティッカーモードに切り替えたら画面を一旦黒で塗りつぶす
        // （天気情報の残像が残らないように）
        tft.fillScreen(ST77XX_BLACK);

        // ティッカーのスクロール位置をリセットして先頭から流し直す
        tickerScrollX = 0;
      }
      else
      {
        currentMode = DisplayMode::WEATHER;
        Serial.println(F("[Button] モード切替 → WEATHER"));

        // 天気モードに戻ったら静的レイアウト（仕切り線など）を再描画
        drawStaticElements();

        // 天気情報も即座に再描画する
        drawWeatherInfo();
      }
    }
  }
  prevModeBtn = modeBtnState;   // 今回の状態を次回の「前回状態」として保存

  // -------------------------------------------------------------------
  // ② 都市切替ボタン（BTN_CITY_PIN）のチェック
  //
  // 動作: 押すたびに次の都市に進みます（最後まで来たら最初に戻る）。
  //       天気モードのときのみ有効にします（ティッカーモード中は無視）。
  // -------------------------------------------------------------------
  int cityBtnState = digitalRead(BTN_CITY_PIN);

  // 立ち下がりエッジ検出（モードボタンと同じ仕組み）
  if (prevCityBtn == HIGH && cityBtnState == LOW)
  {
    if (now - lastPressCityBtn >= BTN_DEBOUNCE_MS)
    {
      lastPressCityBtn = now;

      // 天気表示モードのときだけ都市切替を受け付ける
      if (currentMode == DisplayMode::WEATHER)
      {
        // 都市インデックスを1つ進める（最後→最初のループは % で実現）
        cityIndex = (cityIndex + 1) % cityCount;
        Serial.printf("[Button] 都市手動切替 → %s\n", cities[cityIndex].name);

        // 新しい都市の天気を即座に取得して表示
        updateWeather();
        drawWeatherInfo();
      }
      else
      {
        // ティッカーモード中は都市切替を無視（デバッグ用にログだけ出す）
        Serial.println(F("[Button] ティッカーモード中のため都市切替を無視しました。"));
      }
    }
  }
  prevCityBtn = cityBtnState;
}
