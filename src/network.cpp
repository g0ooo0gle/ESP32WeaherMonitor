/**
 * ESP32 Weather Station - ネットワーク・通信機能 実装
 *
 * [変更点]
 *   - updateWeeklyForecast() を新設
 *     Open-Meteo の daily API で週間天気（最高・最低気温・天気コード）を取得します。
 */

#include "network.h"
#include "cities.h"
#include "display.h"
#include "weather.h"   // DailyForecast 構造体、weeklyForecast[] を使うため

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <time.h>

// ================================================================
// WiFiManager による自動接続
// ================================================================
bool setupWiFi()
{
  WiFiManager wm;

  // autoConnect():
  //   (1) 保存済みSSID/パスワードがあれば自動接続
  //   (2) なければ "ESP32_Weather_Config" という AP を起動
  //       → スマホで接続 → 192.168.4.1 にアクセス → SSID/パスワード設定
  if (!wm.autoConnect("ESP32_Weather_Config"))
  {
    Serial.println(F("[Network] WiFi接続に失敗しました。再起動します..."));
    ESP.restart();
    return false;
  }

  Serial.println(F("[Network] WiFi接続完了! IP:"));
  Serial.println(WiFi.localIP());
  return true;
}

// ================================================================
// NTP による時刻同期設定
// ================================================================
void setupNTP()
{
  // configTime(時差秒, サマータイム秒, NTPサーバー1, NTPサーバー2)
  // JSTはUTC+9 = 9×3600秒。サマータイムは日本にないので0。
  configTime(9 * 3600, 0, "ntp.nict.jp", "time.google.com");
}

// ================================================================
// Open-Meteo API から現在天気データを取得
// ================================================================
bool updateWeather()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(F("[Weather] WiFi未接続のためスキップします。"));
    return false;
  }

  HTTPClient http;
  http.setTimeout(3000);   // 3秒でタイムアウト

  // current_weather=true で現在の気温とWMOコードを取得
  String url = "https://api.open-meteo.com/v1/forecast?latitude="
               + String(cities[cityIndex].lat, 4)
               + "&longitude="
               + String(cities[cityIndex].lon, 4)
               + "&current_weather=true"
               + "&timezone=Asia%2FTokyo";

  Serial.printf("[Weather] 取得: %s\n", cities[cityIndex].name);
  http.begin(url);
  int httpCode = http.GET();
  bool changed = false;

  if (httpCode == HTTP_CODE_OK)
  {
    String payload = http.getString();

    // ArduinoJson でパース（2048バイトのバッファ）
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload);

    if (!err)
    {
      float newTemp = doc["current_weather"]["temperature"].as<float>();
      int   newCode = doc["current_weather"]["weathercode"].as<int>();

      if (currentTemp != newTemp || currentWeatherCode != newCode)
      {
        Serial.printf("[Weather] 更新: %.1f°C code=%d\n", newTemp, newCode);
        currentTemp        = newTemp;
        currentWeatherCode = newCode;
        changed = true;
      }
      else
      {
        Serial.println(F("[Weather] 変化なし。"));
      }
    }
    else
    {
      Serial.println(F("[Weather] JSONパースエラー"));
    }
  }
  else
  {
    Serial.printf("[Weather] HTTP失敗 code=%d\n", httpCode);
  }

  http.end();
  return changed;
}

// ================================================================
// Open-Meteo API から週間天気予報データを取得（新規）
//
// [取得するデータ]
//   daily:
//     - weathercode        : 各日の代表WMO天気コード
//     - temperature_2m_max : 最高気温
//     - temperature_2m_min : 最低気温
//
// [ラベルの付け方]
//   取得した日付を getLocalTime() で取得した今日の日付と比較し、
//   今日なら "今日"、明日なら "明日"、それ以降は曜日（"月"〜"日"）を付けます。
// ================================================================
bool updateWeeklyForecast()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(F("[Weekly] WiFi未接続のためスキップします。"));
    return false;
  }

  HTTPClient http;
  http.setTimeout(5000);   // 週間天気は応答が少し大きいため5秒に設定

  // daily パラメータで最大7日分を取得（past_days=0 で今日から未来のみ）
  String url = "https://api.open-meteo.com/v1/forecast?latitude="
               + String(cities[cityIndex].lat, 4)
               + "&longitude="
               + String(cities[cityIndex].lon, 4)
               + "&daily=weathercode,temperature_2m_max,temperature_2m_min"
               + "&timezone=Asia%2FTokyo"
               + "&forecast_days=7";

  Serial.printf("[Weekly] 取得: %s\n", cities[cityIndex].name);
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("[Weekly] HTTP失敗 code=%d\n", httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  // weekly レスポンスは大きいので 4096 バイトのバッファを用意
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, payload);
  if (err)
  {
    Serial.println(F("[Weekly] JSONパースエラー"));
    return false;
  }

  // daily オブジェクト内の配列を取得
  JsonArray codes   = doc["daily"]["weathercode"];
  JsonArray maxArr  = doc["daily"]["temperature_2m_max"];
  JsonArray minArr  = doc["daily"]["temperature_2m_min"];
  JsonArray dateArr = doc["daily"]["time"];   // "2025-01-01" 形式の文字列配列

  // 今日の日付を取得（ラベル付けに使用）
  struct tm today;
  getLocalTime(&today);

  // データを weeklyForecast[] に格納（最大 WEEKLY_DAYS 日分）
  weeklyDays = 0;
  for (int i = 0; i < (int)codes.size() && i < WEEKLY_DAYS; i++)
  {
    weeklyForecast[i].weatherCode = codes[i].as<int>();
    weeklyForecast[i].tempMax     = maxArr[i].as<float>();
    weeklyForecast[i].tempMin     = minArr[i].as<float>();

    // ラベルを決定する
    // dateArr[i] は "2025-01-15" のような文字列なので、月日だけ切り出す
    const char *dateStr = dateArr[i].as<const char*>();

    if (i == 0)
    {
      // 0日目は必ず今日
      strncpy(weeklyForecast[i].label, "今日", sizeof(weeklyForecast[i].label) - 1);
    }
    else if (i == 1)
    {
      // 1日目は必ず明日
      strncpy(weeklyForecast[i].label, "明日", sizeof(weeklyForecast[i].label) - 1);
    }
    else
    {
      // 2日目以降は曜日を計算して表示（"月" 〜 "日"）
      // ISO日付文字列 "2025-01-15" を tm 構造体に変換
      struct tm t = {};
      // sscanf で年月日を読み取る
      sscanf(dateStr, "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday);
      t.tm_year -= 1900;   // tm_year は 1900 年からの差分
      t.tm_mon  -= 1;      // tm_mon は 0〜11
      mktime(&t);          // tm_wday（曜日）を計算させる

      // 曜日テーブル（0=日, 1=月, ..., 6=土）
      // 短い2バイト文字（1文字）で収めます
      const char *weekdays[] = {"日", "月", "火", "水", "木", "金", "土"};
      strncpy(weeklyForecast[i].label,
              weekdays[t.tm_wday],
              sizeof(weeklyForecast[i].label) - 1);
    }
    weeklyForecast[i].label[sizeof(weeklyForecast[i].label) - 1] = '\0';   // 終端保証

    weeklyDays++;
  }

  Serial.printf("[Weekly] 取得完了: %d日分\n", weeklyDays);
  return true;
}
