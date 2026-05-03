/**
 * ESP32 Weather Station - 物理ボタン制御 実装
 *
 * [このファイルでやること]
 *   1. setupButtons()   GPIO を INPUT_PULLUP で初期化
 *   2. updateButtons()  毎ループ呼ばれ、両ボタンの短押し/長押しを判定
 *
 * [短押しと長押しの判定方法]
 *   ・ボタンが押された瞬間の時刻を記録 (pressStart)
 *   ・押している間ずっと、現在時刻との差を見る
 *   ・500ms を超えた瞬間に「長押し」フラグを立てて長押しアクション実行
 *   ・離した瞬間、長押しフラグが立っていなければ短押しアクション
 *   ・1回の押下で「長押し」と「短押し」が両方発火することはありません
 */

#include "button.h"
#include "display.h"
#include "network.h"
#include "ticker.h"
#include "cities.h"
#include "settings.h"

// ================================================================
// 状態変数の実体定義
// ================================================================
Screen      currentScreen = Screen::WEATHER;        // 起動時は天気画面
DisplayMode currentMode   = DisplayMode::ALL_CITIES; // 起動時は全国巡回
SubView     currentSub    = SubView::WEEKLY;         // 起動時は週間表示

// ================================================================
// 内部状態（このファイルだけで使用）
// ================================================================
static int  prevModeBtn = HIGH;
static int  prevCityBtn = HIGH;

// MODE ボタン用
static unsigned long modePressStart = 0;
static bool          modeLongFired  = false;

// CITY ボタン用
static unsigned long cityPressStart = 0;
static bool          cityLongFired  = false;

// 直前のボタンアクション完了時刻（チャタリング再発火防止）
static unsigned long lastModeAction = 0;
static unsigned long lastCityAction = 0;

// ================================================================
// MODE 短押し: 画面トグル
// ================================================================
static void modeShortPress()
{
  if (currentScreen == Screen::WEATHER) {
    currentScreen = Screen::NEWS;
    Serial.println(F("[Button] MODE短押し: 天気 → ニュース画面"));
    drawNewsScreen();
  } else {
    currentScreen = Screen::WEATHER;
    Serial.println(F("[Button] MODE短押し: ニュース → 天気画面"));
    drawWeatherInfo();
  }
}

// ================================================================
// MODE 長押し: 天気画面で全国 ⇄ 1都市 を切替
// ニュース画面では何もしない
// ================================================================
static void modeLongPress()
{
  if (currentScreen != Screen::WEATHER) {
    Serial.println(F("[Button] MODE長押し: ニュース画面では無効"));
    return;
  }

  if (currentMode == DisplayMode::ALL_CITIES) {
    currentMode = DisplayMode::SINGLE;
    cityIndex   = myCityIndex;
    Serial.printf("[Button] MODE長押し: 地方巡回 → 自分の都市 (%s)\n",
                  cities[cityIndex].name);

    updateWeather();
    if (currentSub == SubView::WEEKLY) updateWeeklyForecast();
    else                                updateHourlyForecast();
  } else {
    currentMode = DisplayMode::ALL_CITIES;
    cityIndex   = getFirstCityInRegion();
    Serial.printf("[Button] MODE長押し: 自分の都市 → 地方巡回 (%s〜)\n",
                  cities[cityIndex].name);

    updateWeather();
    updateWeeklyForecast();
  }

  drawWeatherInfo();
}

// ================================================================
// CITY 短押し: 画面とモードに応じてアクション分岐
// ================================================================
static void cityShortPress()
{
  if (currentScreen == Screen::NEWS) {
    // ニュース画面 → 次の見出しへ
    Serial.println(F("[Button] CITY短押し: 次の見出し"));
    nextNewsPage();
    return;
  }

  // WEATHER画面
  if (currentMode == DisplayMode::ALL_CITIES) {
    // 全国モード → 地方フィルタに従って次の都市
    cityIndex = getNextCityInRegion(cityIndex);
    Serial.printf("[Button] CITY短押し: 都市 → %s\n", cities[cityIndex].name);
    updateWeather();
    drawWeatherInfo();
  } else {
    // 1都市モード → 週間 ⇄ 毎時 をトグル
    if (currentSub == SubView::WEEKLY) {
      currentSub = SubView::HOURLY;
      Serial.println(F("[Button] CITY短押し: 週間 → 毎時"));
      updateHourlyForecast();
    } else {
      currentSub = SubView::WEEKLY;
      Serial.println(F("[Button] CITY短押し: 毎時 → 週間"));
    }
    drawDetailArea();
  }
}

// ================================================================
// CITY 長押し: 画面と状況に応じてアクション分岐
// ================================================================
static void cityLongPress()
{
  if (currentScreen == Screen::NEWS) {
    // ニュース画面 → 前の見出しへ
    Serial.println(F("[Button] CITY長押し: 前の見出し"));
    prevNewsPage();
    return;
  }

  // WEATHER画面 + 1都市モード時のみ都市送り（サブビューは保持）
  if (currentMode == DisplayMode::SINGLE) {
    cityIndex = (cityIndex + 1) % cityCount;
    Serial.printf("[Button] CITY長押し: 都市 → %s\n", cities[cityIndex].name);
    updateWeather();
    if (currentSub == SubView::WEEKLY) updateWeeklyForecast();
    else                                updateHourlyForecast();
    drawWeatherInfo();
  }
  // 全国モード時の長押しは無効（短押しの自動切替で十分なため）
}

// ================================================================
// ボタン GPIO ピンの初期化
// ================================================================
void setupButtons()
{
  pinMode(BTN_MODE_PIN, INPUT_PULLUP);
  pinMode(BTN_CITY_PIN, INPUT_PULLUP);
  Serial.printf("[Button] 初期化完了: MODE=GPIO%d, CITY=GPIO%d\n",
                BTN_MODE_PIN, BTN_CITY_PIN);
}

// ================================================================
// [内部ヘルパー] ボタンの押下/離脱を判定し、短押し/長押しを発火する
//   汎用的に書くと MODE/CITY で同じロジックを共有できます。
//
//   引数:
//     btnState        : 今回読み取ったピン状態 (HIGH/LOW)
//     prevState       : 前回のピン状態 (参照渡しで更新)
//     pressStart      : 押し始め時刻 (参照)
//     longFired       : 長押し発火済みフラグ (参照)
//     lastAction      : 直前のアクション時刻 (チャタリング防止)
//     onShortPress    : 短押し時に呼ぶコールバック
//     onLongPress     : 長押し時に呼ぶコールバック
//     now             : 現在時刻 millis()
// ================================================================
static void handleButton(int btnState, int &prevState,
                         unsigned long &pressStart, bool &longFired,
                         unsigned long &lastAction,
                         void (*onShortPress)(), void (*onLongPress)(),
                         unsigned long now)
{
  // 押し始め (HIGH→LOW)
  if (prevState == HIGH && btnState == LOW) {
    if (now - lastAction >= BTN_DEBOUNCE_MS) {
      pressStart = now;
      longFired  = false;
    }
  }

  // 押し続けている間に長押しを検出
  if (btnState == LOW && !longFired && pressStart != 0) {
    if (now - pressStart >= BTN_LONGPRESS_MS) {
      longFired  = true;
      lastAction = now;
      onLongPress();
    }
  }

  // 離した瞬間 (LOW→HIGH)
  if (prevState == LOW && btnState == HIGH) {
    if (!longFired && pressStart != 0) {
      // 押下時間が短すぎなければ短押しとみなす
      if (now - pressStart >= BTN_DEBOUNCE_MS) {
        lastAction = now;
        onShortPress();
      }
    }
    pressStart = 0;
    longFired  = false;
  }

  prevState = btnState;
}

// ================================================================
// ボタン状態の更新（loop() から毎回呼ぶ）
// ================================================================
void updateButtons()
{
  unsigned long now = millis();

  int modeBtnState = digitalRead(BTN_MODE_PIN);
  int cityBtnState = digitalRead(BTN_CITY_PIN);

  // MODE ボタン
  handleButton(modeBtnState, prevModeBtn,
               modePressStart, modeLongFired, lastModeAction,
               modeShortPress, modeLongPress, now);

  // CITY ボタン
  handleButton(cityBtnState, prevCityBtn,
               cityPressStart, cityLongFired, lastCityAction,
               cityShortPress, cityLongPress, now);
}
