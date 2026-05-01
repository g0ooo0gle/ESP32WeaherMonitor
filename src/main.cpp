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
 *
 */

#include <Arduino.h>
#include <Adafruit_GFX.h>          // コア描画ライブラリ
#include <Adafruit_ST7735.h>       // ST7735専用ライブラリ
#include <U8g2_for_Adafruit_GFX.h> // 日本語用ライブラリ
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <time.h>

// --- ハードウェア設定 (ESP32 VSPI) ---
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

// --- データ構造 ---
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

// 状態管理変数
int cityIndex = 0;
float currentTemp = 0;
int currentWeatherCode = 0;
int cityCount = sizeof(cities) / sizeof(CityData);

// タイマー管理 (millisを利用した非ブロッキング処理)
unsigned long lastWeatherUpdate = 0;
unsigned long lastCitySwitch = 0;
// unsigned long lastTimeUpdate = 0;
unsigned long lastScreenUpdate = 0;

const unsigned long weatherInterval = 15UL * 60UL * 1000UL; // 天気取得: 15分
const unsigned long citySwitchInterval = 15000UL;            // 表示切替: 15秒
// const unsigned long timeUpdateInterval = 500;        // 時計更新: 0.5秒
const unsigned long screenInterval = 1000UL; // 1秒（画面更新）

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

// 天気情報取得（Wi‑Fi が接続されていることが前提）
void updateWeather()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  HTTPClient http;
  // 緯度・経度・タイムゾーンを指定してJSONを取得
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(cities[cityIndex].lat) +
               "&longitude=" + String(cities[cityIndex].lon) +
               "&current_weather=true&timezone=Asia%2FTokyo";

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK)
  {
    String payload = http.getString();
    // 適切なサイズに変更。足りない場合は増やす。
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err)
    {
      currentTemp = doc["current_weather"]["temperature"].as<float>();
      currentWeatherCode = doc["current_weather"]["weathercode"].as<int>();
      lastWeatherUpdate = millis(); // 成功したら更新時刻を記録
    }
    else
    {
      Serial.println(F("JSON parse error"));
    }
  }
  else
  {
    Serial.printf("HTTP GET failed, code: %d\n", httpCode);
  }
  http.end();

}

/* 画面全体の再描画（毎秒呼び出す） */
void drawFullScreen()
{
  // 時計領域（上部）
  tft.fillRect(0, 0, 128, 32, ST77XX_BLACK);
  tft.drawFastHLine(0, 30, 128, ST77XX_WHITE);

  // 都市名領域
  tft.fillRect(0, 32, 128, 36, ST77XX_BLACK);

  // 気温領域
  tft.fillRect(0, 68, 128, 64, ST77XX_BLACK);

  // 天気説明領域
  tft.fillRect(0, 132, 128, 28, ST77XX_BLACK);

  // --- 時計 ---
  struct tm ti;
  if (getLocalTime(&ti))
  {
    char timeStr[16];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &ti);
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(2);
    tft.setCursor(15, 6);
    tft.print(timeStr);
  }
  else
  {
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(2);
    tft.setCursor(8, 6);
    tft.print("--:--:--");
  }

  // --- 都市名（日本語） ---
  u8g2.setFont(u8g2_font_unifont_t_japanese1);
  u8g2.setForegroundColor(ST77XX_YELLOW);
  // u8g2 のカーソル位置はピクセルで指定（ベースラインに注意）
  u8g2.setCursor(8, 55);
  String cityLabel = String("【") + String(cities[cityIndex].name) + String("】");
  // u8g2.print(cityLabel);
  u8g2.print(cityLabel.c_str()); // ←String → const char* へ変換

  // --- 気温 ---
  u8g2.setFont(u8g2_font_logisoso32_tr);
  u8g2.setForegroundColor(ST77XX_CYAN);
  u8g2.setCursor(8, 110);
  // 小数点1桁
  char tempBuf[16];
  snprintf(tempBuf, sizeof(tempBuf), "%.1f", currentTemp);
  u8g2.print(tempBuf);
  u8g2.setFont(u8g2_font_unifont_t_japanese1);
  u8g2.print("°C");

  // --- 天気説明 ---
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(8, 150);
  // u8g2.print("天気: " + getWeatherJp(currentWeatherCode));
  String weatherStr = getWeatherJp(currentWeatherCode);
  u8g2.print(weatherStr.c_str()); // ←String → const char* へ変換
}

void setup()
{
  Serial.begin(115200);

  // LCD初期化
  // TFTディスプレイと日本語ライブラリの開始
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0); // 縦向きに変更 (128x160)
  tft.fillScreen(ST77XX_BLACK);
  u8g2.begin(tft);

  // WiFi接続 (未設定時は "ESP32_Weather_Config" というAPになる)
  u8g2.setFont(u8g2_font_unifont_t_japanese1);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(0, 80);
  u8g2.print("WiFi setup...");
  Serial.println("wifi setup...");

  // 1. WiFiManager起動
  WiFiManager wm;
  if (!wm.autoConnect("ESP32_Weather_Config"))
  {
    Serial.println("Failed to connect");
    ESP.restart();
  }

  tft.fillScreen(ST77XX_BLACK);
  u8g2.setCursor(0, 80);
  u8g2.print("WiFi接続完了！");
  Serial.println("connected.");

  // 2. 時刻同期 (NTP)
  tft.fillScreen(ST77XX_BLACK);
  u8g2.setCursor(0, 80);
  u8g2.print("時刻同期中...");
  Serial.println("time setup...");

  // NTPサーバーの設定 (日本標準時)
  configTime(9 * 3600, 0, "ntp.nict.jp", "time.google.com");
  Serial.println("time setup done.");

  // 最初の天気取得
  updateWeather();
  // 初回画面描画
  drawFullScreen();

  // タイマー初期化
  lastWeatherUpdate = millis();
  lastCitySwitch = millis();
  lastScreenUpdate = millis();

  // drawMainUI();
}

void loop()
{
  unsigned long now = millis();

  // 1秒毎に画面を再描画（時計＋その他を毎秒更新）
  if (now - lastScreenUpdate >= screenInterval)
  {
    drawFullScreen();
    lastScreenUpdate = now;
  }

  // 都市切替（8秒）
  if (now - lastCitySwitch >= citySwitchInterval)
  {
    cityIndex = (cityIndex + 1) % cityCount;
    // 新しい都市の天気を即座に取得（非頻繁）
    updateWeather();
    // 次の秒の更新で画面が変わる（既に毎秒更新なのでここではdrawしなくてOK）
    lastCitySwitch = now;
  }

  // 定期天気更新（15分）
  if (now - lastWeatherUpdate >= weatherInterval)
  {
    updateWeather();
    // lastWeatherUpdate は updateWeather() 成功時に更新されます
    // 失敗対策が必要ならここで再試行ロジックを入れてください
  }

  // ここで delay は使わない（ノンブロッキング）

}

