/**
 * network.cpp - ネットワーク・通信機能実装
 *
 * WiFi 接続、NTP 時刻同期、Open-Meteo API による天気データ取得を担当します。
 * 天気 API の HTTP 取得は Core 0 タスクで非同期実行し、
 * Core 1 のボタン処理・画面描画をブロックしません。
 *
 * [マルチコア同期]
 *   - weatherFetchReq / weatherFetchReady の読み書きは portMUX で保護します。
 *   - currentTemp / currentWeatherCode は 32bit アトミック書き込みが保証される
 *     float / int のため、追加の同期は不要です。
 *   - 配列データ (weeklyForecast 等) の書き込み後は __sync_synchronize() で
 *     メモリ順序を保証してから ready フラグを立てます。
 */

#include "network.h"
#include "cities.h"
#include "display.h"
#include "weather.h"
#include "settings.h"

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
    Serial.println(F("[Network] WiFi 接続失敗。再起動します..."));
    ESP.restart();
    return false;
  }

  Serial.print(F("[Network] WiFi 接続完了 IP:"));
  Serial.println(WiFi.localIP());
  return true;
}

// ================================================================
// NTP による時刻同期
// configTime() 後に最大 20 秒 (500ms × 40) 同期完了を待ちます。
// タイムアウトしても処理は続行されます（起動直後の天気取得は時刻なしで実行）。
// ================================================================
void setupNTP()
{
  configTime(9 * 3600, 0, "ntp.nict.jp", "time.google.com");

  struct tm ti;
  for (int i = 0; i < 40; i++) {
    if (getLocalTime(&ti, 500)) {
      Serial.printf("[NTP] 同期完了: %04d-%02d-%02d %02d:%02d:%02d\n",
                    ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                    ti.tm_hour, ti.tm_min, ti.tm_sec);
      return;
    }
  }
  Serial.println(F("[NTP] タイムアウト - 時刻未同期のまま続行"));
}

// ================================================================
// Open-Meteo API から現在天気データを取得
// 戻り値: データが変化した場合 true
// ================================================================
bool updateWeather()
{
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[Weather] WiFi 未接続のためスキップ"));
    return false;
  }

  HTTPClient http;
  http.setTimeout(5000);

  String url = "https://api.open-meteo.com/v1/forecast?latitude="
               + String(getActiveLat(), 4)
               + "&longitude="
               + String(getActiveLon(), 4)
               + "&current_weather=true"
               + "&timezone=Asia%2FTokyo";

  Serial.printf("[Weather] 取得: %s\n", getActiveName());
  http.begin(url);
  int httpCode = http.GET();
  bool changed = false;

  if (httpCode == HTTP_CODE_OK)
  {
    // ESP32 HTTPS では getStream() をそのまま渡すと SSL バッファリングの影響で
    // JSON が途中で途切れることがある。getString() で一括取得してからパースする。
    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);

    if (!err)
    {
      float newTemp = doc["current_weather"]["temperature"].as<float>();
      int   newCode = doc["current_weather"]["weathercode"].as<int>();

      if (currentTemp != newTemp || currentWeatherCode != newCode) {
        Serial.printf("[Weather] 更新: %.1f°C code=%d\n", newTemp, newCode);
        currentTemp        = newTemp;
        currentWeatherCode = newCode;
        changed = true;
      } else {
        Serial.println(F("[Weather] 変化なし"));
      }
    }
    else
    {
      Serial.printf("[Weather] JSON パースエラー: %s\n", err.c_str());
    }
  }
  else
  {
    Serial.printf("[Weather] HTTP 失敗 code=%d\n", httpCode);
    http.end();
  }

  return changed;
}

// ================================================================
// Open-Meteo API から週間天気予報データを取得
// ================================================================
bool updateWeeklyForecast()
{
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[Weekly] WiFi 未接続のためスキップ"));
    return false;
  }

  HTTPClient http;
  http.setTimeout(8000);

  String url = "https://api.open-meteo.com/v1/forecast?latitude="
               + String(getActiveLat(), 4)
               + "&longitude="
               + String(getActiveLon(), 4)
               + "&daily=weathercode,temperature_2m_max,temperature_2m_min"
               + "&timezone=Asia%2FTokyo"
               + "&forecast_days=7";

  Serial.printf("[Weekly] 取得: %s\n", getActiveName());
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[Weekly] HTTP 失敗 code=%d\n", httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[Weekly] JSON パースエラー: %s\n", err.c_str());
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

    if (i == 0) {
      strncpy(weeklyForecast[i].label, "今日", sizeof(weeklyForecast[i].label) - 1);
    } else if (i == 1) {
      strncpy(weeklyForecast[i].label, "明日", sizeof(weeklyForecast[i].label) - 1);
    } else {
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

  Serial.printf("[Weekly] 取得完了: %d 日分\n", weeklyDays);
  return true;
}

// ================================================================
// Open-Meteo API から毎時天気予報データを取得
//
// forecast_hours=7 で現在時刻から 7 時間分 (HOURLY_HOURS+1) のみ取得します。
// 不要な 48 時間分を取得しないため、通信量・メモリ使用量を大幅に削減できます。
//
// API の time 配列は "YYYY-MM-DDTHH:MM" 形式で、先頭が最も現在に近い時刻です。
// ================================================================
bool updateHourlyForecast()
{
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[Hourly] WiFi 未接続のためスキップ"));
    return false;
  }

  HTTPClient http;
  http.setTimeout(8000);

  // forecast_hours=7: 現在から 7 時間先まで (6 時間表示 + 現在 1 時間のマージン)
  String url = "https://api.open-meteo.com/v1/forecast?latitude="
               + String(getActiveLat(), 4)
               + "&longitude="
               + String(getActiveLon(), 4)
               + "&hourly=temperature_2m,weathercode"
               + "&timezone=Asia%2FTokyo"
               + "&forecast_hours=7";

  Serial.printf("[Hourly] 取得: %s\n", getActiveName());
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[Hourly] HTTP 失敗 code=%d\n", httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[Hourly] JSON パースエラー: %s\n", err.c_str());
    return false;
  }

  JsonArray timeArr = doc["hourly"]["time"];
  JsonArray tempArr = doc["hourly"]["temperature_2m"];
  JsonArray codeArr = doc["hourly"]["weathercode"];

  struct tm now;
  if (!getLocalTime(&now)) {
    Serial.println(F("[Hourly] 時刻取得失敗"));
    return false;
  }
  int currentHour = now.tm_hour;
  int currentDay  = now.tm_mday;

  // 現在時刻に一致するエントリを先頭から検索する
  int startIndex = -1;
  for (int i = 0; i < (int)timeArr.size(); i++) {
    const char *tstr = timeArr[i].as<const char*>();
    int day, hour;
    if (sscanf(tstr, "%*d-%*d-%dT%d", &day, &hour) == 2) {
      if (day == currentDay && hour == currentHour) {
        startIndex = i;
        break;
      }
    }
  }

  if (startIndex == -1) {
    // forecast_hours=7 で取得しているため現在時刻は先頭付近にあるはず。
    // 見つからない場合は先頭から使用する。
    startIndex = 0;
    Serial.println(F("[Hourly] 現在時刻エントリが見つからないため先頭から使用"));
  }

  hourlyHours = 0;
  for (int i = 0; i < HOURLY_HOURS; i++) {
    int idx = startIndex + i;
    if (idx >= (int)timeArr.size()) break;

    hourlyForecast[i].weatherCode = codeArr[idx].as<int>();
    hourlyForecast[i].temp        = tempArr[idx].as<float>();

    if (i == 0) {
      strncpy(hourlyForecast[i].label, "今", sizeof(hourlyForecast[i].label) - 1);
    } else {
      const char *tstr = timeArr[idx].as<const char*>();
      int day, hour;
      sscanf(tstr, "%*d-%*d-%dT%d", &day, &hour);
      snprintf(hourlyForecast[i].label, sizeof(hourlyForecast[i].label), "%d時", hour);
    }
    hourlyForecast[i].label[sizeof(hourlyForecast[i].label) - 1] = '\0';
    hourlyHours++;
  }

  Serial.printf("[Hourly] 取得完了: %d 時間分\n", hourlyHours);
  return true;
}

// ================================================================
// 非同期天気取得タスク (Core 0)
//
// [設計]
//   - loop() (Core 1) から requestWeatherFetch(flags) で取得を予約する。
//   - weatherFetchTask (Core 0) がフラグを確認して HTTP 取得を実行する。
//   - 完了後 weatherFetchReady に bitmask をセットし、
//     loop() が drawWeatherInfo() を呼ぶ。
//
// [同期]
//   weatherFetchReq / weatherFetchReady の読み書きは portMUX で保護し、
//   Core 0/1 間の競合を防ぎます。
// ================================================================
volatile bool    weatherFetchBusy  = false;
volatile uint8_t weatherFetchReady = 0;

static volatile uint8_t weatherFetchReq = 0;
static portMUX_TYPE     s_fetchMux      = portMUX_INITIALIZER_UNLOCKED;

void requestWeatherFetch(uint8_t flags)
{
  portENTER_CRITICAL(&s_fetchMux);
  weatherFetchReq |= flags;
  portEXIT_CRITICAL(&s_fetchMux);
}

static void weatherFetchTask(void*)
{
  for (;;) {
    portENTER_CRITICAL(&s_fetchMux);
    uint8_t req     = weatherFetchReq;
    weatherFetchReq = 0;
    portEXIT_CRITICAL(&s_fetchMux);

    if (req) {
      weatherFetchBusy = true;

      uint8_t ready = 0;
      if (req & FETCH_CURRENT) { if (updateWeather())         ready |= FETCH_CURRENT; }
      if (req & FETCH_WEEKLY)  { if (updateWeeklyForecast())  ready |= FETCH_WEEKLY;  }
      if (req & FETCH_HOURLY)  { if (updateHourlyForecast())  ready |= FETCH_HOURLY;  }

      __sync_synchronize();   // 配列書き込みの完了を ready セット前に保証

      portENTER_CRITICAL(&s_fetchMux);
      weatherFetchReady |= ready;
      portEXIT_CRITICAL(&s_fetchMux);

      weatherFetchBusy = false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void startWeatherFetchTask()
{
  xTaskCreatePinnedToCore(
    weatherFetchTask, "wxFetch",
    8192, nullptr, 1, nullptr, 0
  );
  Serial.println(F("[Weather] 非同期取得タスク起動 (Core 0)"));
}
