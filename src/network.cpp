/**
 * ESP32 Weather Station - ネットワーク・通信機能 実装
 */

#include "network.h"
#include "cities.h"
#include "display.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <time.h>

// ================================================================
// WiFiManager による自動接続
// 初回起動時はAPモードで設定画面を表示し、2回目以降は自動接続。
// ================================================================
bool setupWiFi()
{
  // WiFiManager のインスタンスを作成（ESP32のWiFi設定を管理する）
  WiFiManager wm;

  // autoConnect() を呼ぶと以下のように動作します:
  //   (1) 以前保存したSSID/パスワードがあれば、それを自動接続
  //   (2) 保存がない場合、"ESP32_Weather_Config" というAPを生成
  //       → スマホでWiFiに接続し、浏览器で192.168.4.1にアクセスすると
  //         SSID/パスワードの設定画面が表示される
  //   (3) 正しいSSID/パスワードで接続できたら、APモードを自動的に終了
  if (!wm.autoConnect("ESP32_Weather_Config"))
  {
    // 接続失敗 → リセットして再起動（接続設定を再度促す）
    Serial.println(F("[Network] WiFi接続に失敗しました。再起動します..."));
    ESP.restart();
    return false;
  }

  // 接続成功
  Serial.println(F("[Network] WiFi接続完了! IP取得:"));
  Serial.println(WiFi.localIP());
  return true;
}

// ================================================================
// NTP（Network Time Protocol）による時刻同期
// NICT（情報通信研究機構）とGoogleのNTPサーバーに接続し、
// 日本標準時（JST = UTC+9）を取得します。
// ================================================================
void setupNTP()
{
  // configTime() の引数:
  //   1 個目: 時差（秒単位）→ JSTはUTC+9なので 9*3600
  //   2 個目: サマータイムのオフセット（今回は日本に不要なので0）
  //   3 個目 以降: NTPサーバーのホスト名（2つ以上指定すると、
  //                 一つが動かなければもう一つにフォールバック）
  configTime(9 * 3600, 0, "ntp.nict.jp", "time.google.com");
}

// ================================================================
// Open-Meteo API から天気データを取得して更新
// 取得するデータ:
//   - temperature（気温）: 今の気温（摂氏）
//   - weathercode（WMO天気コード）: 天気の数値コード（0=快晴, 61=小雨, など）
//
// 戻り値が true の場合のみ、loop() 側で画面を再描画します。
// これにより、データが同じ値の場合は無駄な描画が行われません。
// ================================================================
bool updateWeather()
{
  // WiFiが未接続なら、取得できないので即座にfalseを返す
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(F("[Weather] WiFi未接続のため取得をスキップします。"));
    return false;
  }

  // HTTPクライアントを作成（3秒でタイムアウト。ハング防止）
  HTTPClient http;
  http.setTimeout(3000);

  // Open-Meteo API のURLを構築
  // latitude: 緯度、longitude: 経度、current_weather: 現在天気をリクエスト
  // timezone: Asia/Tokyo（APIのレスポンス時刻をJSTに合わせます）
  String url = "https://api.open-meteo.com/v1/forecast?latitude="
               + String(cities[cityIndex].lat)
               + "&longitude="
               + String(cities[cityIndex].lon)
               + "&current_weather=true"
               + "&timezone=Asia%2FTokyo";

  Serial.printf("[Weather] 取得開始: %s\n", url.c_str());
  http.begin(url);
  int httpCode = http.GET();
  bool changed = false; // データが変化したかどうか

  // HTTPレスポンスが正常（200 OK）の場合
  if (httpCode == HTTP_CODE_OK)
  {
    // レスポンス本文（JSON文字列）を取得
    String payload = http.getString();

    // ArduinoJsonでJSONをパース（メモリ確保は2048バイト）
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload);

    if (!err) // パース成功
    {
      // JSONから気温と天気コードを取得
      float newTemp = doc["current_weather"]["temperature"].as<float>();
      int newCode = doc["current_weather"]["weathercode"].as<int>();

      // 値に変化があった場合のみ更新
      // 気温または天気コードのいずれかが変わったら changed = true
      if (currentTemp != newTemp || currentWeatherCode != newCode)
      {
        Serial.printf("[Weather] データ変化: %.1f°C code=%d → %.1f°C code=%d\n",
                      currentTemp, currentWeatherCode, newTemp, newCode);
        currentTemp = newTemp;
        currentWeatherCode = newCode;
        changed = true;
      }
      else
      {
        Serial.println(F("[Weather] データ変化なし。"));
      }
    }
    else
    {
      // JSONパースに失敗した場合
      Serial.println(F("[Weather] JSONパースエラー"));
    }
  }
  else
  {
    // HTTPエラー発生（404, 500, タイムアウト  など）
    Serial.printf("[Weather] HTTP FAILED, code: %d\n", httpCode);
  }

  http.end();  // 接続を閉じてメモリを解放
  return changed;  // 変化があれば true
}
