/**
 * ESP32 Weather Station - 物理ボタン制御 実装
 *
 * [3ボタン構成・短押しのみ]
 *   SCREEN (GPIO  0) : 天気 ⇄ ニュース 切替
 *   MODE   (GPIO 35) : 地方巡回 ⇄ 自分の都市 切替（天気画面のみ有効）
 *   NEXT   (GPIO 34) : 次へ（コンテキスト依存）
 *
 * [長押し廃止の理由]
 *   2ボタン×短押し/長押しの組み合わせは直感的でなかったため、
 *   3ボタン短押しのみに統一してシンプル化。
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
Screen      currentScreen = Screen::WEATHER;         // 起動時は天気画面
DisplayMode currentMode   = DisplayMode::ALL_CITIES; // 起動時は地方巡回
SubView     currentSub    = SubView::WEEKLY;          // 起動時は週間表示

// ================================================================
// 内部状態
// ================================================================
static int           prevScreenBtn = HIGH;
static int           prevModeBtn   = HIGH;
static int           prevNextBtn   = HIGH;

static unsigned long screenPressStart = 0;
static unsigned long modePressStart   = 0;
static unsigned long nextPressStart   = 0;

static unsigned long lastScreenAction = 0;
static unsigned long lastModeAction   = 0;
static unsigned long lastNextAction   = 0;

// ================================================================
// SCREEN ボタン: 天気 ⇄ ニュース 切替
// ================================================================
static void screenPress()
{
  if (currentScreen == Screen::WEATHER) {
    currentScreen = Screen::NEWS;
    Serial.println(F("[Button] SCREEN: 天気 → ニュース"));
    drawNewsScreen();
  } else {
    currentScreen = Screen::WEATHER;
    Serial.println(F("[Button] SCREEN: ニュース → 天気"));
    drawWeatherInfo();
  }
}

// ================================================================
// MODE ボタン: 地方巡回 ⇄ 自分の都市 切替（天気画面のみ）
// ================================================================
static void modePress()
{
  if (currentScreen != Screen::WEATHER) return;

  if (currentMode == DisplayMode::ALL_CITIES) {
    currentMode = DisplayMode::SINGLE;
    cityIndex   = myCityIndex;
    Serial.printf("[Button] MODE: 地方巡回 → 自分の都市 (%s)\n",
                  cities[cityIndex].name);
    updateWeather();
    if (currentSub == SubView::WEEKLY) updateWeeklyForecast();
    else                                updateHourlyForecast();
  } else {
    currentMode = DisplayMode::ALL_CITIES;
    cityIndex   = getFirstCityInRegion();
    Serial.printf("[Button] MODE: 自分の都市 → 地方巡回 (%s〜)\n",
                  cities[cityIndex].name);
    updateWeather();
    updateWeeklyForecast();
  }

  drawWeatherInfo();
}

// ================================================================
// NEXT ボタン: コンテキスト依存の「次へ」
//   ニュース画面          → 次の見出し
//   天気 / 地方巡回モード  → 次の都市（地方フィルタに従う）
//   天気 / 自分の都市モード → 週間 ⇄ 毎時 切替
// ================================================================
static void nextPress()
{
  if (currentScreen == Screen::NEWS) {
    Serial.println(F("[Button] NEXT: 次の見出し"));
    nextNewsPage();
    return;
  }

  if (currentMode == DisplayMode::ALL_CITIES) {
    cityIndex = getNextCityInRegion(cityIndex);
    Serial.printf("[Button] NEXT: 次の都市 → %s\n", cities[cityIndex].name);
    updateWeather();
    drawWeatherInfo();
  } else {
    if (currentSub == SubView::WEEKLY) {
      currentSub = SubView::HOURLY;
      Serial.println(F("[Button] NEXT: 週間 → 毎時"));
      updateHourlyForecast();
    } else {
      currentSub = SubView::WEEKLY;
      Serial.println(F("[Button] NEXT: 毎時 → 週間"));
    }
    drawDetailArea();
  }
}

// ================================================================
// [内部ヘルパー] 短押し判定（長押しなし版）
//
//   引数:
//     btnState   : 今回読み取ったピン状態 (HIGH/LOW)
//     prevState  : 前回のピン状態 (参照渡しで更新)
//     pressStart : 押し始め時刻 (参照)
//     lastAction : 直前のアクション時刻 (チャタリング防止)
//     onPress    : 押下時に呼ぶコールバック
//     now        : 現在時刻 millis()
// ================================================================
static void handleButton(int btnState, int &prevState,
                         unsigned long &pressStart,
                         unsigned long &lastAction,
                         void (*onPress)(),
                         unsigned long now)
{
  // 押し始め (HIGH→LOW)
  if (prevState == HIGH && btnState == LOW) {
    if (now - lastAction >= BTN_DEBOUNCE_MS) {
      pressStart = now;
    }
  }

  // 離した瞬間 (LOW→HIGH) かつデバウンス時間を満たしていれば発火
  if (prevState == LOW && btnState == HIGH) {
    if (pressStart != 0 && now - pressStart >= BTN_DEBOUNCE_MS) {
      lastAction = now;
      onPress();
    }
    pressStart = 0;
  }

  prevState = btnState;
}

// ================================================================
// ボタン GPIO ピンの初期化
// ================================================================
void setupButtons()
{
  pinMode(BTN_SCREEN_PIN, INPUT_PULLUP);
  pinMode(BTN_MODE_PIN,   INPUT_PULLUP);
  pinMode(BTN_NEXT_PIN,   INPUT_PULLUP);   // GPIO 34: 外付けプルアップ推奨
  Serial.printf("[Button] 初期化完了: SCREEN=GPIO%d, MODE=GPIO%d, NEXT=GPIO%d\n",
                BTN_SCREEN_PIN, BTN_MODE_PIN, BTN_NEXT_PIN);
}

// ================================================================
// ボタン状態の更新（loop() から毎回呼ぶ）
// ================================================================
void updateButtons()
{
  unsigned long now = millis();

  int screenState = digitalRead(BTN_SCREEN_PIN);
  int modeState   = digitalRead(BTN_MODE_PIN);
  int nextState   = digitalRead(BTN_NEXT_PIN);

  handleButton(screenState, prevScreenBtn, screenPressStart, lastScreenAction, screenPress, now);
  handleButton(modeState,   prevModeBtn,   modePressStart,   lastModeAction,   modePress,   now);
  handleButton(nextState,   prevNextBtn,   nextPressStart,   lastNextAction,   nextPress,   now);
}
