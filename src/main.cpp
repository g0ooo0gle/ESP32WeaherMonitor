/**
 * ESP32 Weather Station
 * ESP32 + ST7735S 1.8inch Display Project
 *
 * [機能]
 * 1. WiFiManagerによる自動接続
 * 2. NTPによる秒単位の正確な時刻表示
 * 3. Open-Meteo APIによる日本全国主要都市の巡回
 * 4. 全天気コード(WMO)の完全日本語化
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
 */

#include <Arduino.h>
#include <Adafruit_GFX.h>          // コア描画ライブラリ
#include <Adafruit_ST7735.h>       // ST7735専用ライブラリ
#include <U8g2_for_Adafruit_GFX.h> // 日本語フォント用ライブラリ
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <time.h>

// --- ピン配置の定義 ---
// ESP32の標準SPI（VSPI）ピンを使用
#define TFT_SCLK 18 // SCL (Clock)
#define TFT_MOSI 23 // SDA (Data)
#define TFT_RST 4   // RESET
#define TFT_DC 2    // A0 (Data/Command)
#define TFT_CS 5    // CS (Chip Select)

// インスタンスの作成
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2; // 日本語フォント用インスタンス

// --- レイアウト定数（変更時にここだけ直す） ---
// 各エリアのY座標と高さ(H)を一元管理することで、
// レイアウト変更時の修正箇所を最小化する
#define AREA_CLOCK_Y 0     // 時計エリア 開始Y
#define AREA_CLOCK_H 30    // 時計エリア 高さ
#define AREA_CITY_Y 31     // 都市名エリア 開始Y（仕切り線の直下）
#define AREA_CITY_H 24     // 都市名エリア 高さ
#define AREA_TEMP_Y 58     // 気温エリア 開始Y
#define AREA_TEMP_H 60     // 気温エリア 高さ（大きめフォント分）
#define AREA_WEATHER_Y 122 // 天気説明エリア 開始Y
#define AREA_WEATHER_H 38  // 天気説明エリア 高さ

// --- 都市データ構造 (日本の主要都市を地方別に網羅) ---
struct CityData
{
  const char *name;
  float lat;
  float lon;
};

const CityData cities[] = {
    {"札幌", 43.0642, 141.3468},   // 北海道
    {"仙台", 38.2682, 140.8694},   // 東北
    {"東京", 35.6895, 139.6917},   // 関東
    {"新潟", 37.9162, 139.0364},   // 北陸
    {"名古屋", 35.1815, 136.9064}, // 中部
    {"大阪", 34.6937, 135.5023},   // 近畿
    {"広島", 34.3852, 132.4553},   // 中国
    {"高松", 34.3427, 134.0466},   // 四国
    {"福岡", 33.5902, 130.4017},   // 九州
    {"那覇", 26.2124, 127.6761}    // 沖縄
};

// --- 状態管理変数 ---
int cityIndex = 0;                                 // 現在表示中の都市インデックス
float currentTemp = 0;                             // 現在の気温
int currentWeatherCode = 0;                        // 現在のWMO天気コード
int cityCount = sizeof(cities) / sizeof(CityData); // 都市数

// --- 差分検出用キャッシュ ---
// 前回描画した値を保持し、変化があった時だけ再描画することでちらつきを防ぐ
char prevTimeStr[16] = ""; // 前回描画した時刻文字列
int prevCityIndex = -1;    // 前回の都市インデックス（-1 = 未描画）
float prevTemp = -999;     // 前回の気温（ありえない初期値で初回強制描画）
int prevWeatherCode = -1;  // 前回の天気コード（-1 = 未描画）

// --- タイマー管理 (millisを利用した非ブロッキング処理) ---
unsigned long lastFetchAttempt = 0; // API通信の前回試行時刻
unsigned long lastCitySwitch = 0;   // 都市切替の前回実行時刻
unsigned long lastClockUpdate = 0;  // 時計描画の前回実行時刻

const unsigned long fetchInterval = 15UL * 60UL * 1000UL; // 天気取得間隔: 15分
const unsigned long citySwitchInterval = 15000UL;         // 都市切替間隔: 15秒
const unsigned long clockInterval = 500UL;                // 時計更新間隔: 0.5秒（差分描画なので高頻度でもOK）

// --- WMO天気コードを日本語テキストに完全変換 ---
// Open-Meteoが採用している国際基準のコード表をすべて網羅
String getWeatherJp(int code)
{
  switch (code)
  {
  case 0:
    return "快晴";
  case 1:
    return "概ね晴れ";
  case 2:
    return "時々曇り";
  case 3:
    return "くもり";
  case 45:
    return "霧";
  case 48:
    return "着氷性の霧";
  case 51:
    return "軽い霧雨";
  case 53:
    return "霧雨";
  case 55:
    return "濃い霧雨";
  case 56:
    return "軽い氷霧雨";
  case 57:
    return "濃い氷霧雨";
  case 61:
    return "小雨";
  case 63:
    return "雨";
  case 65:
    return "強い雨";
  case 66:
    return "軽い氷雨";
  case 67:
    return "強い氷雨";
  case 71:
    return "小雪";
  case 73:
    return "雪";
  case 75:
    return "強い雪";
  case 77:
    return "粒雪";
  case 80:
    return "にわか雨(弱)";
  case 81:
    return "にわか雨";
  case 82:
    return "激しい雨";
  case 85:
    return "軽い雪のシャワー";
  case 86:
    return "激しい雪のシャワー";
  case 95:
    return "雷雨";
  case 96:
    return "雷雨と軽い雹";
  case 99:
    return "雷雨と激しい雹";
  default:
    return "不明 (" + String(code) + ")";
  }
}

// --- ユーティリティ ---
// 指定エリアを背景色（デフォルト黒）で塗りつぶすヘルパー関数
// 再描画前の残像消去に使用する
inline void clearArea(int y, int h, uint16_t color = ST77XX_BLACK)
{
  tft.fillRect(0, y, 128, h, color);
}

// --- 個別エリア描画関数 ---

// 時計エリアのみ再描画
// strcmp による差分チェックで、秒が変わった時だけ描画するためちらつきがない
void drawClock()
{
  struct tm ti;
  // NTP同期が完了していない場合は描画しない
  if (!getLocalTime(&ti))
    return;

  char timeStr[16];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &ti);

  // 前回と同じ文字列なら描画をスキップ（ちらつき防止の核心部分）
  if (strcmp(timeStr, prevTimeStr) == 0)
    return;
  strcpy(prevTimeStr, timeStr); // キャッシュを更新

  // 時計エリアのみ消去してから再描画（画面全体クリアより高速）
  clearArea(AREA_CLOCK_Y, AREA_CLOCK_H);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(15, 6);
  tft.print(timeStr);
  Serial.printf("[Clock] %s\n", timeStr); // デバッグ: 描画が発生した秒を確認
}

// 都市名エリアのみ再描画
// 都市切替タイミングでのみ呼ばれる（頻度が低いためキャッシュ不要）
void drawCity()
{
  clearArea(AREA_CITY_Y, AREA_CITY_H);
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  u8g2.setForegroundColor(ST77XX_YELLOW);
  // Stringを一度変数に格納しメモリ確保を明示（ポインター破綻防止）
  String label = String(cities[cityIndex].name);
  u8g2.setCursor(8, AREA_CITY_Y + AREA_CITY_H - 4);     // ベースライン: エリア下端から4px上
  u8g2.print(label.c_str());                            // String → const char* へ変換して渡す
  Serial.printf("[City] %s\n", cities[cityIndex].name); // デバッグ: 都市切替を確認
}

// 気温エリアのみ再描画
// 天気データ更新時・都市切替時のみ呼ばれる
void drawTemp()
{
  clearArea(AREA_TEMP_Y, AREA_TEMP_H);
  u8g2.setFont(u8g2_font_logisoso32_tr); // 大型数字フォント
  u8g2.setForegroundColor(ST77XX_CYAN);
  u8g2.setCursor(8, AREA_TEMP_Y + AREA_TEMP_H - 4); // ベースライン: エリア下端から4px上
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", currentTemp); // 小数点1桁で整形
  u8g2.print(buf);
  // 「度」だけ日本語フォントに切り替えて続けて描画
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  u8g2.print("度");
}

// 天気説明エリアのみ再描画
// 天気データ更新時・都市切替時のみ呼ばれる
void drawWeather()
{
  clearArea(AREA_WEATHER_Y, AREA_WEATHER_H);
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(8, AREA_WEATHER_Y + AREA_WEATHER_H - 4); // ベースライン: エリア下端から4px上
  String ws = getWeatherJp(currentWeatherCode);
  u8g2.print(ws.c_str()); // String → const char* へ変換して渡す
}

// 気温・天気説明をまとめて更新し、キャッシュを同期する
// 都市切替時・定期取得でデータが変化した時に呼ぶ
void drawWeatherInfo()
{
  drawTemp();
  drawWeather();
  // キャッシュを現在値で更新（次回の差分チェックに使用）
  prevTemp = currentTemp;
  prevWeatherCode = currentWeatherCode;
  Serial.printf("[Weather] Temp: %.1f, Code: %d (%s)\n",
                currentTemp, currentWeatherCode,
                getWeatherJp(currentWeatherCode).c_str()); // デバッグ: 描画内容を確認
}

// 起動時に一度だけ呼ぶ静的要素の描画
// 仕切り線など変化しない要素はここで描いて以後は触らない
void drawStaticElements()
{
  tft.fillScreen(ST77XX_BLACK);
  tft.drawFastHLine(0, 30, 128, ST77XX_WHITE); // 時計エリアと情報エリアの区切り線
}

// --- 天気情報取得（Wi-Fi が接続されていることが前提） ---
// 戻り値: データが実際に変化した場合 true（再描画の要否判定に使用）
bool updateWeather()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(F("[Weather] WiFi not connected, skip fetch."));
    return false;
  }

  HTTPClient http;
  http.setTimeout(3000); // 通信ハング防止: 3秒でタイムアウト

  // 緯度・経度・タイムゾーンを指定してJSONを取得
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(cities[cityIndex].lat) + "&longitude=" + String(cities[cityIndex].lon) + "&current_weather=true&timezone=Asia%2FTokyo";

  Serial.printf("[Weather] Fetching: %s\n", url.c_str()); // デバッグ: リクエストURLを確認
  http.begin(url);
  int httpCode = http.GET();
  bool changed = false;

  if (httpCode == HTTP_CODE_OK)
  {
    String payload = http.getString();
    // 適切なサイズに変更。足りない場合は増やす。
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err)
    {
      float newTemp = doc["current_weather"]["temperature"].as<float>();
      int newCode = doc["current_weather"]["weathercode"].as<int>();

      // 値が変化した場合のみ更新（変化がなければ再描画も不要）
      if (currentTemp != newTemp || currentWeatherCode != newCode)
      {
        Serial.printf("[Weather] Data changed: %.1f°C code=%d -> %.1f°C code=%d\n",
                      currentTemp, currentWeatherCode, newTemp, newCode); // デバッグ: 変化量を確認
        currentTemp = newTemp;
        currentWeatherCode = newCode;
        changed = true;
      }
      else
      {
        Serial.println(F("[Weather] Data unchanged.")); // デバッグ: データ変化なし
      }
    }
    else
    {
      Serial.println(F("[Weather] JSON parse error")); // デバッグ: JSONパース失敗
    }
  }
  else
  {
    Serial.printf("[Weather] HTTP GET failed, code: %d\n", httpCode); // デバッグ: HTTPエラーコードを確認
  }
  http.end();
  return changed; // データが実際に変わった時だけ true を返す
}

void setup()
{
  Serial.begin(115200);
  Serial.println(F("\n[Setup] ESP32 Weather Station starting..."));

  // TFTディスプレイと日本語ライブラリの開始
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0); // 縦向き (128x160)
  tft.fillScreen(ST77XX_BLACK);
  u8g2.begin(tft);

  // --- WiFi接続（未設定時は "ESP32_Weather_Config" というAPになる） ---
  u8g2.setFont(u8g2_font_b16_t_japanese3);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(0, 80);
  u8g2.print("WiFi setup...");
  Serial.println(F("[Setup] Starting WiFiManager..."));

  // WiFiManager起動: 保存済み認証情報があれば自動接続、なければAPを立ててポータル表示
  WiFiManager wm;
  if (!wm.autoConnect("ESP32_Weather_Config"))
  {
    Serial.println(F("[Setup] Failed to connect. Restarting..."));
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

  // NTPサーバーの設定（日本標準時: UTC+9, 夏時間なし）
  configTime(9 * 3600, 0, "ntp.nict.jp", "time.google.com");
  Serial.println(F("[Setup] NTP configured."));

  // --- 静的レイアウトを一度だけ描画 ---
  drawStaticElements();

  // --- タイマー初期化 ---
  lastFetchAttempt = millis() - fetchInterval; // 初回は即座に天気取得を試みる
  lastCitySwitch = millis();
  lastClockUpdate = millis();

  // --- 初回データ取得 & 全エリア描画 ---
  Serial.println(F("[Setup] Initial weather fetch..."));
  updateWeather();
  drawCity();        // 都市名エリアを描画
  drawWeatherInfo(); // 気温・天気説明エリアを描画

  Serial.println(F("[Setup] Setup complete. Entering loop."));
}

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
    cityIndex = (cityIndex + 1) % cityCount; // 最後の都市の次は先頭に戻る
    Serial.printf("[Loop] Switching city -> %s\n", cities[cityIndex].name);
    updateWeather();   // 新しい都市の天気を即座に取得
    drawCity();        // 都市名を即座に更新
    drawWeatherInfo(); // 気温・天気も即座に更新
    prevCityIndex = cityIndex;
    lastCitySwitch = now;
  }

  // ③ 定期天気更新（15分ごと）
  // データが変化した場合のみ再描画するので、都市切替とは独立して動作する
  if (now - lastFetchAttempt >= fetchInterval)
  {
    Serial.println(F("[Loop] Periodic weather update..."));
    bool changed = updateWeather();
    if (changed)
    {
      drawWeatherInfo(); // 変化があった場合のみ再描画（ちらつき防止）
    }
    lastFetchAttempt = now; // 成否にかかわらずタイマーをリセット（レート制限対策）
  }

  // ここで delay は使わない（ノンブロッキング設計）
}