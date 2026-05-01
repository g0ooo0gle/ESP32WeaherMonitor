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
unsigned long lastTimeUpdate = 0;

const unsigned long weatherInterval = 15 * 60 * 1000; // 天気取得: 15分
const unsigned long citySwitchInterval = 8000;        // 表示切替: 8秒
const unsigned long timeUpdateInterval = 1000;        // 時計更新: 1秒

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

// --- 関数プロトタイプ ---
void updateWeather();
void displayLayout();

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
    JsonDocument doc;
    deserializeJson(doc, http.getString());
    currentTemp = doc["current_weather"]["temperature"];
    currentWeatherCode = doc["current_weather"]["weathercode"];
  }
  http.end();
  lastWeatherUpdate = millis();
}

// 画面全体の再描画 (都市切り替え時に実行)
void drawMainUI()
{
  tft.fillScreen(ST77XX_BLACK);
  tft.drawFastHLine(0, 26, 160, ST77XX_WHITE); // 上部境界線

  // 気温の表示 (大きなフォント)
  u8g2.setFont(u8g2_font_logisoso24_tr);
  u8g2.setForegroundColor(ST77XX_CYAN);
  u8g2.setCursor(10, 75);
  u8g2.print(String(currentTemp, 1));
  u8g2.setFont(u8g2_font_unifont_t_japanese1);
  u8g2.print(" ℃");

  // 天気状態の表示
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 110);
  u8g2.print("天気: " + getWeatherJp(currentWeatherCode));

  // 現在の都市名表示 (下部)
  u8g2.setForegroundColor(ST77XX_YELLOW);
  u8g2.setCursor(35, 145);
  u8g2.print("-- " + String(cities[cityIndex].name) + " --");
}

// 時計のみを部分更新 (毎秒実行)
void updateClockDisplay()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
    return;

  char timeStr[20];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);

  // 時計表示エリア(100x20)のみを黒塗りで消去して上書き
  tft.fillRect(5, 5, 120, 20, ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(5, 5);
  tft.print(timeStr);
}

void setup()
{
  Serial.begin(115200);

  // LCD初期化
  // TFTディスプレイと日本語ライブラリの開始
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  u8g2.begin(tft);

  // WiFi接続 (未設定時は "ESP32_Weather_Config" というAPになる)
  u8g2.setFont(u8g2_font_unifont_t_japanese1);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(0, 40);
  u8g2.print("WiFi設定を待機中...");
  Serial.println("wifi setup...");

  // 1. WiFiManager起動

  WiFiManager wm;
  if (!wm.autoConnect("ESP32_Weather_Config"))
  {
    Serial.println("Failed to connect");
    ESP.restart();
  }
  tft.fillScreen(ST77XX_BLACK);
  u8g2.setCursor(0, 40);
  u8g2.print("WiFi接続完了！");
  Serial.println("connected.");

  // 2. 時刻同期 (NTP)
  tft.fillScreen(ST77XX_BLACK);
  u8g2.setCursor(0, 40);
  u8g2.print("時刻同期中...");
  Serial.println("time setup...");

  // NTPサーバーの設定 (日本標準時)
  configTime(9 * 3600, 0, "ntp.nict.jp", "time.google.com");
  Serial.println("time setup done.");

  // 初期データの取得と描画
  updateWeather();
  drawMainUI();
}

void loop()
{
  unsigned long now = millis();

  // 【1】1秒ごとの時刻更新処理
  if (now - lastTimeUpdate >= timeUpdateInterval)
  {
    updateClockDisplay();
    lastTimeUpdate = now;
  }

  // 【2】指定秒ごとの都市切り替え処理
  if (now - lastCitySwitch >= citySwitchInterval)
  {
    cityIndex = (cityIndex + 1) % cityCount;
    updateWeather(); // 新しい都市の天気を取得
    drawMainUI();    // 画面全体を書き換え
    lastCitySwitch = now;
  }
  // 【3】15分ごとの最新天気データ取得 (バックグラウンド更新用)
  if (now - lastWeatherUpdate >= weatherInterval)
  {
    updateWeather();
    // 描画は次の都市切り替えタイミングで行われる
  }

  // 15分おきにデータ更新
  if (millis() - lastWeatherUpdate > weatherInterval)
  {
    updateWeather();
  }

  // 都市の巡回（簡易実装）
  displayLayout();
  delay(5000);
  cityIndex = (cityIndex + 1) % (sizeof(cities) / sizeof(CityData));

}

void displayLayout()
{
  // ここに時刻のリアルタイム更新などを記述
}