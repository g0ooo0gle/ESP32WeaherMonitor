/**
 * ESP32 Weather Station - 設定管理 実装
 *
 * [NVS 名前空間 "wx_cfg"]
 *   "myCity"    : 登録都市インデックス (int)
 *   "region"    : 巡回する地方フィルタ (int, -1=全国)
 *   "useCustom" : カスタム都市を使用するか (int, 0/1)
 *   "cusName"   : カスタム都市名 (String)
 *   "cusLat"    : カスタム緯度 (float)
 *   "cusLon"    : カスタム経度 (float)
 *
 * [Web 設定ページ]
 *   GET  /       → 設定フォームを表示
 *   POST /save   → 設定を NVS へ保存し、GET / へリダイレクト
 */

#include "settings.h"
#include "display.h"   // cityIndex, prevTimeStr
#include "button.h"    // currentMode, DisplayMode

#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

// ================================================================
// 設定変数の実体
// ================================================================
int  myCityIndex     = 12;           // デフォルト: 東京
int  regionFilter    = FILTER_ALL;   // デフォルト: 全国
bool settingsChanged = false;

char  customCityName[32] = "";
float customCityLat      = 35.6895f;   // デフォルト: 東京
float customCityLon      = 139.6917f;
bool  useCustomCity      = false;

// ================================================================
// 内部変数
// ================================================================
static Preferences prefs;
static WebServer   server(80);

static const char* NVS_NS          = "wx_cfg";
static const char* KEY_CITY        = "myCity";
static const char* KEY_REGION      = "region";
static const char* KEY_USE_CUSTOM  = "useCustom";
static const char* KEY_CUS_NAME    = "cusName";
static const char* KEY_CUS_LAT     = "cusLat";
static const char* KEY_CUS_LON     = "cusLon";

static const char* REGION_LABELS[] = {
  "北海道・東北地方", "関東地方", "中部地方",
  "関西地方", "中国・四国地方", "九州・沖縄地方"
};

// ================================================================
// 設定の読み込み
// ================================================================
void loadSettings()
{
  prefs.begin(NVS_NS, /*readonly=*/true);
  myCityIndex   = prefs.getInt(KEY_CITY,       12);
  regionFilter  = prefs.getInt(KEY_REGION,     FILTER_ALL);
  useCustomCity = prefs.getInt(KEY_USE_CUSTOM, 0) != 0;
  customCityLat = prefs.getFloat(KEY_CUS_LAT,  35.6895f);
  customCityLon = prefs.getFloat(KEY_CUS_LON,  139.6917f);
  String cn     = prefs.getString(KEY_CUS_NAME, "");
  cn.toCharArray(customCityName, sizeof(customCityName));
  prefs.end();

  if (myCityIndex < 0 || myCityIndex >= cityCount) myCityIndex = 12;
  if (regionFilter < FILTER_ALL || regionFilter > FILTER_KYUSHU) regionFilter = FILTER_ALL;

  Serial.printf("[Settings] 読込: 都市=%s, 地方=%d, カスタム=%d(%s)\n",
                cities[myCityIndex].name, regionFilter,
                useCustomCity, customCityName);
}

// ================================================================
// 設定の保存
// ================================================================
void saveSettings()
{
  prefs.begin(NVS_NS, /*readonly=*/false);
  prefs.putInt(KEY_CITY,       myCityIndex);
  prefs.putInt(KEY_REGION,     regionFilter);
  prefs.putInt(KEY_USE_CUSTOM, useCustomCity ? 1 : 0);
  prefs.putString(KEY_CUS_NAME, customCityName);
  prefs.putFloat(KEY_CUS_LAT,  customCityLat);
  prefs.putFloat(KEY_CUS_LON,  customCityLon);
  prefs.end();

  Serial.printf("[Settings] 保存: 都市=%s, 地方=%d, カスタム=%d(%s)\n",
                cities[myCityIndex].name, regionFilter,
                useCustomCity, customCityName);
}

// ================================================================
// 自分の都市モードのアクセサ
// useCustomCity && SINGLE モードのときカスタム値を返す
// ================================================================
const char* getActiveName()
{
  if (useCustomCity && currentMode == DisplayMode::SINGLE)
    return (customCityName[0] != '\0') ? customCityName : "カスタム都市";
  return cities[cityIndex].name;
}

float getActiveLat()
{
  if (useCustomCity && currentMode == DisplayMode::SINGLE) return customCityLat;
  return cities[cityIndex].lat;
}

float getActiveLon()
{
  if (useCustomCity && currentMode == DisplayMode::SINGLE) return customCityLon;
  return cities[cityIndex].lon;
}

// ================================================================
// 都市フィルタ判定
// ================================================================
bool cityMatchesFilter(int cityIdx)
{
  if (cityIdx < 0 || cityIdx >= cityCount) return false;
  if (regionFilter == FILTER_ALL) return true;

  RegionBlock r = cities[cityIdx].region;
  switch (regionFilter) {
    case FILTER_HOKKAIDO_TOHOKU: return r == REGION_HOKKAIDO || r == REGION_TOHOKU;
    case FILTER_KANTO:           return r == REGION_KANTO;
    case FILTER_CHUBU:           return r == REGION_CHUBU;
    case FILTER_KINKI:           return r == REGION_KINKI;
    case FILTER_CHUGOKU:         return r == REGION_CHUGOKU || r == REGION_SHIKOKU;
    case FILTER_KYUSHU:          return r == REGION_KYUSHU  || r == REGION_OKINAWA;
    default: return true;
  }
}

int getNextCityInRegion(int current)
{
  for (int i = 1; i <= cityCount; i++) {
    int idx = (current + i) % cityCount;
    if (cityMatchesFilter(idx)) return idx;
  }
  return current;
}

int getFirstCityInRegion()
{
  for (int i = 0; i < cityCount; i++) {
    if (cityMatchesFilter(i)) return i;
  }
  return 0;
}

// ================================================================
// Web サーバー: GET / — 設定フォーム
// ================================================================
static void handleRoot()
{
  bool saved = server.hasArg("saved");

  String html;
  html.reserve(4500);

  // ---- HTML ヘッダ + CSS ----
  html = F("<!DOCTYPE html><html lang='ja'><head>"
           "<meta charset='UTF-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>ESP32 天気設定</title>"
           "<style>"
           "body{font-family:sans-serif;max-width:440px;margin:40px auto;padding:0 16px;"
           "background:#1a1a2e;color:#eee}"
           "h1{color:#4fc3f7;margin-bottom:4px}"
           "p.sub{color:#90a4ae;font-size:13px;margin:0 0 20px}"
           "label{display:block;margin-top:20px;font-weight:bold;color:#90caf9}"
           "select{width:100%;padding:9px 8px;margin-top:6px;background:#16213e;color:#eee;"
           "border:1px solid #4fc3f7;border-radius:6px;font-size:15px}"
           ".rg{margin-top:8px;font-size:14px;color:#b0bec5;display:flex;gap:20px}"
           ".rg label{margin-top:0;font-weight:normal;color:#b0bec5;cursor:pointer}"
           ".ti{width:100%;padding:9px 8px;margin-top:6px;background:#16213e;color:#eee;"
           "border:1px solid #4fc3f7;border-radius:6px;font-size:15px;box-sizing:border-box}"
           ".lrow{display:flex;gap:10px;margin-top:2px}"
           ".lrow>div{flex:1}"
           ".ni{width:100%;padding:9px 6px;margin-top:4px;background:#16213e;color:#eee;"
           "border:1px solid #78909c;border-radius:6px;font-size:14px;box-sizing:border-box}"
           "small{display:block;color:#90a4ae;font-size:11px;margin-top:10px}"
           ".hint{color:#607d8b;font-size:11px;margin-top:6px}"
           ".btn{margin-top:28px;width:100%;padding:14px;background:#4fc3f7;color:#1a1a2e;"
           "border:none;border-radius:6px;font-size:16px;font-weight:bold;cursor:pointer}"
           ".btn:active{background:#0288d1}"
           ".ok{margin-top:16px;padding:10px 14px;background:#0d4f3c;"
           "border-radius:6px;color:#66bb6a;font-size:14px}"
           ".note{margin-top:28px;padding:10px 14px;background:#1e2a3a;"
           "border-radius:6px;color:#90a4ae;font-size:12px;line-height:1.6}"
           "</style></head><body>"
           "<h1>⚙ 天気設定</h1>"
           "<p class='sub'>ESP32 Weather Monitor</p>");

  if (saved) html += F("<div class='ok'>✓ 設定を保存しました</div>");

  html += F("<form method='POST' action='/save'>");

  // ---- 自分の都市: ラジオ選択 ----
  html += F("<label>自分の都市</label>"
            "<div class='rg'>"
            "<label><input type='radio' name='city_mode' value='preset'");
  if (!useCustomCity) html += F(" checked");
  html += F("> 登録都市から選ぶ</label>"
            "<label><input type='radio' name='city_mode' value='custom'");
  if (useCustomCity) html += F(" checked");
  html += F("> カスタム設定</label></div>");

  // ---- 登録都市ドロップダウン ----
  html += F("<div id='dp'><select name='city'>");
  for (int i = 0; i < cityCount; i++) {
    html += F("<option value='");
    html += i;
    html += '\'';
    if (i == myCityIndex) html += F(" selected");
    html += '>';
    html += cities[i].name;
    html += F("</option>");
  }
  html += F("</select></div>");

  // ---- カスタム都市入力欄 ----
  html += F("<div id='dc'>"
            "<small>都市名</small>"
            "<input class='ti' type='text' name='custom_name'"
            " placeholder='例: 川崎市' maxlength='31' value='");
  html += customCityName;
  html += F("'>"
            "<div class='lrow'>"
            "<div><small>緯度</small>"
            "<input class='ni' type='number' name='custom_lat'"
            " step='0.0001' min='-90' max='90' placeholder='35.6895' value='");
  html += String(customCityLat, 4);
  html += F("'></div>"
            "<div><small>経度</small>"
            "<input class='ni' type='number' name='custom_lon'"
            " step='0.0001' min='-180' max='180' placeholder='139.6917' value='");
  html += String(customCityLon, 4);
  html += F("'></div></div>"
            "<small class='hint'>緯度・経度は Google マップで確認できます</small>"
            "</div>");

  // ---- JS: ラジオ切替で div を表示/非表示 ----
  html += F("<script>"
            "function tm(m){"
            "document.getElementById('dp').style.display=m==='preset'?'block':'none';"
            "document.getElementById('dc').style.display=m==='custom'?'block':'none';}"
            "document.querySelectorAll('[name=city_mode]')"
            ".forEach(r=>r.onchange=e=>tm(e.target.value));"
            "tm('");
  html += useCustomCity ? "custom" : "preset";
  html += F("');</script>");

  // ---- 巡回する地方 ----
  html += F("<label>巡回表示する地方</label>"
            "<select name='region'>"
            "<option value='-1'");
  if (regionFilter == FILTER_ALL) html += F(" selected");
  html += F(">全国</option>");
  for (int i = FILTER_HOKKAIDO_TOHOKU; i <= FILTER_KYUSHU; i++) {
    html += F("<option value='");
    html += i;
    html += '\'';
    if (i == regionFilter) html += F(" selected");
    html += '>';
    html += REGION_LABELS[i];
    html += F("</option>");
  }
  html += F("</select>");

  // ---- 保存ボタン ----
  html += F("<button class='btn' type='submit'>保存する</button></form>");

  // ---- ボタン操作の説明 ----
  html += F("<div class='note'>"
            "<b>ボタン操作（短押しのみ）</b><br>"
            "SCREEN &nbsp;→ 天気 ⇄ ニュース<br>"
            "MODE &nbsp;&nbsp;&nbsp;→ 地方巡回 ⇄ 自分の都市<br>"
            "NEXT（天気/巡回中）&nbsp;&nbsp;&nbsp;→ 次の都市<br>"
            "NEXT（天気/自分の都市）→ 週間 ⇄ 毎時<br>"
            "NEXT（ニュース）&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;→ 次の見出し"
            "</div>");

  html += F("</body></html>");

  server.send(200, F("text/html; charset=UTF-8"), html);
}

// ================================================================
// Web サーバー: POST /save — 設定保存
// ================================================================
static void handleSave()
{
  // ---- 自分の都市 ----
  String cityMode = server.hasArg("city_mode") ? server.arg("city_mode") : "preset";
  useCustomCity = (cityMode == "custom");

  if (!useCustomCity) {
    // 登録都市
    if (server.hasArg("city")) {
      int v = server.arg("city").toInt();
      if (v >= 0 && v < cityCount) myCityIndex = v;
    }
  } else {
    // カスタム都市
    if (server.hasArg("custom_name")) {
      String cn = server.arg("custom_name");
      cn.trim();
      cn.toCharArray(customCityName, sizeof(customCityName));
    }
    if (server.hasArg("custom_lat")) {
      float v = server.arg("custom_lat").toFloat();
      if (v >= -90.0f && v <= 90.0f) customCityLat = v;
    }
    if (server.hasArg("custom_lon")) {
      float v = server.arg("custom_lon").toFloat();
      if (v >= -180.0f && v <= 180.0f) customCityLon = v;
    }
  }

  // ---- 巡回する地方 ----
  if (server.hasArg("region")) {
    int v = server.arg("region").toInt();
    if (v >= FILTER_ALL && v <= FILTER_KYUSHU) regionFilter = v;
  }

  saveSettings();

  // cityIndex を即時調整
  if (currentMode == DisplayMode::SINGLE) {
    if (!useCustomCity) cityIndex = myCityIndex;
    // カスタムの場合 cityIndex はそのまま（座標はアクセサで返す）
  } else {
    if (!cityMatchesFilter(cityIndex)) cityIndex = getFirstCityInRegion();
  }

  // 都市名表示キャッシュをリセット → drawClockCity() が必ず再描画
  prevTimeStr[0] = '\0';

  settingsChanged = true;

  server.sendHeader("Location", "/?saved=1");
  server.send(302);
}

// ================================================================
// Web サーバー初期化
// ================================================================
void setupWebServer()
{
  server.on("/",     HTTP_GET,  handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.printf("[WebServer] 起動: http://%s/\n",
                WiFi.localIP().toString().c_str());
}

// ================================================================
// Web サーバー クライアント処理（loop() から毎回呼ぶ）
// ================================================================
void handleWebServer()
{
  server.handleClient();
}
