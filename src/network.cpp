/**
 * ESP32 Weather Station - ネットワーク・通信機能 実装
 *
 * [今回の変更点]
 *   - updateHourlyForecast() を新設
 *     Open-Meteo の hourly API で毎時の気温と天気コードを取得し、
 *     現在時刻以降の6時間分を hourlyForecast[] に格納します。
 */

#include "network.h"
#include "cities.h"
#include "display.h"
#include "weather.h"

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
  http.setTimeout(3000);

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
// Open-Meteo API から週間天気予報データを取得
// ================================================================
bool updateWeeklyForecast()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(F("[Weekly] WiFi未接続のためスキップします。"));
    return false;
  }

  HTTPClient http;
  http.setTimeout(5000);

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

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, payload);
  if (err)
  {
    Serial.println(F("[Weekly] JSONパースエラー"));
    return false;
  }

  JsonArray codes   = doc["daily"]["weathercode"];
  JsonArray maxArr  = doc["daily"]["temperature_2m_max"];
  JsonArray minArr  = doc["daily"]["temperature_2m_min"];
  JsonArray dateArr = doc["daily"]["time"];

  weeklyDays = 0;
  for (int i = 0; i < (int)codes.size() && i < WEEKLY_DAYS; i++)
  {
    weeklyForecast[i].weatherCode = codes[i].as<int>();
    weeklyForecast[i].tempMax     = maxArr[i].as<float>();
    weeklyForecast[i].tempMin     = minArr[i].as<float>();

    const char *dateStr = dateArr[i].as<const char*>();

    if (i == 0)
    {
      strncpy(weeklyForecast[i].label, "今日", sizeof(weeklyForecast[i].label) - 1);
    }
    else if (i == 1)
    {
      strncpy(weeklyForecast[i].label, "明日", sizeof(weeklyForecast[i].label) - 1);
    }
    else
    {
      struct tm t = {};
      sscanf(dateStr, "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday);
      t.tm_year -= 1900;
      t.tm_mon  -= 1;
      mktime(&t);

      const char *weekdays[] = {"日", "月", "火", "水", "木", "金", "土"};
      strncpy(weeklyForecast[i].label,
              weekdays[t.tm_wday],
              sizeof(weeklyForecast[i].label) - 1);
    }
    weeklyForecast[i].label[sizeof(weeklyForecast[i].label) - 1] = '\0';

    weeklyDays++;
  }

  Serial.printf("[Weekly] 取得完了: %d日分\n", weeklyDays);
  return true;
}

// ================================================================
// Open-Meteo API から毎時天気予報データを取得（新規）
//
// [取得するデータ]
//   hourly:
//     - time           : "2025-01-15T14:00" 形式の時刻文字列配列
//     - temperature_2m : 各時刻の気温
//     - weathercode    : 各時刻のWMO天気コード
//
// [処理の流れ]
//   1. APIで24時間分のhourlyデータを取得（forecast_days=2 で48時間分）
//   2. 取得した時刻配列の中から「現在時刻と同じか直後の時刻」を探す
//   3. そこから6時間分を hourlyForecast[] に格納
//
// [ラベルの付け方]
//   先頭は "今" 、それ以降は "HH時"（例: "14時"）
// ================================================================
bool updateHourlyForecast()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(F("[Hourly] WiFi未接続のためスキップします。"));
    return false;
  }

  HTTPClient http;
  http.setTimeout(8000);   // 毎時データは多めなので8秒

  // hourly パラメータで2日分（48時間）を取得
  String url = "https://api.open-meteo.com/v1/forecast?latitude="
               + String(cities[cityIndex].lat, 4)
               + "&longitude="
               + String(cities[cityIndex].lon, 4)
               + "&hourly=temperature_2m,weathercode"
               + "&timezone=Asia%2FTokyo"
               + "&forecast_days=2";

  Serial.printf("[Hourly] 取得: %s\n", cities[cityIndex].name);
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("[Hourly] HTTP失敗 code=%d\n", httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  // 48時間分の hourly データはサイズが大きいので 16KB のバッファを確保
  // ESP32 はヒープに余裕があるが、念のため使い終わったらすぐ解放されます。
  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, payload);
  if (err)
  {
    Serial.printf("[Hourly] JSONパースエラー: %s\n", err.c_str());
    return false;
  }

  JsonArray timeArr  = doc["hourly"]["time"];
  JsonArray tempArr  = doc["hourly"]["temperature_2m"];
  JsonArray codeArr  = doc["hourly"]["weathercode"];

  // 現在時刻を取得（startIndex を決めるのに使う）
  struct tm now;
  if (!getLocalTime(&now)) {
    Serial.println(F("[Hourly] 時刻取得失敗"));
    return false;
  }
  int currentHour = now.tm_hour;
  int currentDay  = now.tm_mday;

  // 現在時刻と一致するエントリを探す
  // API の time は "2025-01-15T14:00" のフォーマット
  int startIndex = -1;
  for (int i = 0; i < (int)timeArr.size(); i++)
  {
    const char *tstr = timeArr[i].as<const char*>();
    // "YYYY-MM-DDTHH:MM" の HH 部分を切り出す
    int day, hour;
    if (sscanf(tstr, "%*d-%*d-%dT%d", &day, &hour) == 2)
    {
      if (day == currentDay && hour == currentHour)
      {
        startIndex = i;
        break;
      }
    }
  }

  if (startIndex == -1) {
    Serial.println(F("[Hourly] 現在時刻のエントリが見つかりません"));
    return false;
  }

  // startIndex から HOURLY_HOURS 個ぶんを格納
  hourlyHours = 0;
  for (int i = 0; i < HOURLY_HOURS; i++)
  {
    int idx = startIndex + i;
    if (idx >= (int)timeArr.size()) break;

    hourlyForecast[i].weatherCode = codeArr[idx].as<int>();
    hourlyForecast[i].temp        = tempArr[idx].as<float>();

    // ラベル決定
    if (i == 0) {
      strncpy(hourlyForecast[i].label, "今", sizeof(hourlyForecast[i].label) - 1);
    } else {
      const char *tstr = timeArr[idx].as<const char*>();
      int day, hour;
      sscanf(tstr, "%*d-%*d-%dT%d", &day, &hour);
      // "14時" のような表示
      snprintf(hourlyForecast[i].label, sizeof(hourlyForecast[i].label),
               "%d時", hour);
    }
    hourlyForecast[i].label[sizeof(hourlyForecast[i].label) - 1] = '\0';

    hourlyHours++;
  }

  Serial.printf("[Hourly] 取得完了: %d時間分\n", hourlyHours);
  return true;
}
