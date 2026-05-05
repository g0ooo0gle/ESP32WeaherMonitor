/**
 * button.cpp - 物理ボタン制御実装
 *
 * 3 ボタン・短押しのみのシンプルな操作体系です。
 *
 *   ボタン  GPIO  役割
 *   ────────────────────────────────────────────
 *   SCREEN    0   天気 ⇄ ニュース 切替
 *   MODE     35   地方巡回 ⇄ 自分の都市 切替（天気画面のみ）
 *   NEXT     34   次へ（コンテキスト依存）
 *
 * [GPIO 34/35 について]
 *   入力専用ピンのため INPUT_PULLUP は動作しません。
 *   10kΩ 外付けプルアップ（→ 3.3V）を接続してください。
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
Screen      currentScreen = Screen::WEATHER;
DisplayMode currentMode   = DisplayMode::ALL_CITIES;
SubView     currentSub    = SubView::WEEKLY;

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
    // NEWS 表示中は天気の定期取得を停止しているため、復帰時に即時取得する
    requestWeatherFetch(FETCH_CURRENT | FETCH_WEEKLY);
    unsigned long now = millis();
    lastFetchAttempt = now;
    lastWeeklyFetch  = now;
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
    drawWeatherInfo();
    showLoadingOverlay("天気 取得中...");
    uint8_t flags = FETCH_CURRENT |
                    ((currentSub == SubView::HOURLY) ? FETCH_HOURLY : FETCH_WEEKLY);
    requestWeatherFetch(flags);
  } else {
    currentMode = DisplayMode::ALL_CITIES;
    cityIndex   = getFirstCityInRegion();
    currentSub  = SubView::WEEKLY;
    Serial.printf("[Button] MODE: 自分の都市 → 地方巡回 (%s〜)\n",
                  cities[cityIndex].name);
    drawWeatherInfo();
    showLoadingOverlay("天気 取得中...");
    requestWeatherFetch(FETCH_CURRENT | FETCH_WEEKLY);
  }
}

// ================================================================
// NEXT ボタン: コンテキスト依存の「次へ」
//   ニュース画面           → 次の見出し
//   天気 / 地方巡回モード  → 次の都市
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
    drawWeatherInfo();
    showLoadingOverlay("天気 取得中...");
    unsigned long now = millis();
    lastFetchAttempt = now;
    lastWeeklyFetch  = now;
    lastCitySwitch   = now;
    requestWeatherFetch(FETCH_CURRENT | FETCH_WEEKLY);
  } else {
    if (currentSub == SubView::WEEKLY) {
      currentSub = SubView::HOURLY;
      Serial.println(F("[Button] NEXT: 週間 → 毎時"));
      drawWeatherInfo();
      showLoadingOverlay("毎時天気 取得中...");
      requestWeatherFetch(FETCH_HOURLY);
    } else {
      currentSub = SubView::WEEKLY;
      Serial.println(F("[Button] NEXT: 毎時 → 週間"));
      drawDetailArea();
    }
  }
}

// ================================================================
// [内部] 短押し判定ヘルパー
// ================================================================
static void handleButton(int btnState, int &prevState,
                         unsigned long &pressStart,
                         unsigned long &lastAction,
                         void (*onPress)(),
                         unsigned long now)
{
  if (prevState == HIGH && btnState == LOW) {
    if (now - lastAction >= BTN_DEBOUNCE_MS) {
      pressStart = now;
    }
  }

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
// GPIO 34/35 は INPUT_PULLUP 非対応のため INPUT を使用します。
// ================================================================
void setupButtons()
{
  pinMode(BTN_SCREEN_PIN, INPUT_PULLUP);
  pinMode(BTN_MODE_PIN,   INPUT);   // GPIO 35: 外付けプルアップ使用
  pinMode(BTN_NEXT_PIN,   INPUT);   // GPIO 34: 外付けプルアップ必須
  Serial.printf("[Button] 初期化: SCREEN=GPIO%d, MODE=GPIO%d, NEXT=GPIO%d\n",
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
