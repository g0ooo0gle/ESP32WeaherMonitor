/**
 * settings.cpp - 設定管理実装
 *
 * NVS (Preferences) による設定の永続化と
 * ブラウザ経由の Web 設定ページを提供します。
 *
 * [NVS 名前空間 "wx_cfg"]
 *   "myCity"    : 登録都市インデックス (int)
 *   "region"    : 巡回する地方フィルタ (int, -1 = 全国)
 *   "useCustom" : カスタム都市を使用するか (int, 0/1)
 *   "cusName"   : カスタム都市名 (String)
 *   "cusLat"    : カスタム緯度 (float)
 *   "cusLon"    : カスタム経度 (float)
 *   "rssUrl"    : ニュース RSS フィード URL (String)
 *
 * [Web ページ]
 *   GET  /           → 設定フォーム（都市・地方・RSS URL）
 *   POST /save       → 設定を NVS へ保存し GET / へリダイレクト
 *   GET  /dashboard  → 接続情報ダッシュボード（自動更新 10 秒）
 *   GET  /system     → 再起動・WiFi リセットページ
 *   POST /restart    → ESP32 再起動
 *   POST /reset-wifi → WiFi 設定削除 + 再起動
 */

#include "settings.h"
#include "display.h"
#include "button.h"

#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>

// ================================================================
// 設定変数の実体
// ================================================================
int  myCityIndex     = 12;           // デフォルト: 東京
int  regionFilter    = FILTER_ALL;
bool settingsChanged = false;

char  customCityName[32] = "";
float customCityLat      = 35.6895f;
float customCityLon      = 139.6917f;
bool  useCustomCity      = false;

char rssUrl[RSS_URL_MAX] = "https://www3.nhk.or.jp/rss/news/cat0.xml";

// ================================================================
// 内部変数
// ================================================================
static Preferences prefs;
static WebServer   server(80);

static const char* NVS_NS         = "wx_cfg";
static const char* KEY_CITY       = "myCity";
static const char* KEY_REGION     = "region";
static const char* KEY_USE_CUSTOM = "useCustom";
static const char* KEY_CUS_NAME   = "cusName";
static const char* KEY_CUS_LAT    = "cusLat";
static const char* KEY_CUS_LON    = "cusLon";
static const char* KEY_RSS_URL    = "rssUrl";

static const char* DEFAULT_RSS    = "https://www3.nhk.or.jp/rss/news/cat0.xml";

static const char* REGION_LABELS[] = {
  "北海道・東北地方", "関東地方", "中部地方",
  "関西地方", "中国・四国地方", "九州・沖縄地方"
};

// ================================================================
// [内部] HTML 特殊文字をエスケープしてセキュアに出力する
// ================================================================
static String htmlEscape(const char *s)
{
  String r;
  r.reserve(strlen(s) + 16);
  while (*s) {
    switch (*s) {
      case '&':  r += F("&amp;");  break;
      case '<':  r += F("&lt;");   break;
      case '>':  r += F("&gt;");   break;
      case '"':  r += F("&quot;"); break;
      case '\'': r += F("&#39;");  break;
      default:   r += *s;          break;
    }
    s++;
  }
  return r;
}

// ================================================================
// [内部] 共通 CSS + ナビゲーションバーを生成する
// activePage: "settings" / "dashboard" / "system"
// ================================================================
static String buildPageHeader(const char* title, const char* activePage)
{
  String h;
  h.reserve(1400);
  h = F("<!DOCTYPE html><html lang='ja'><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>");
  h += title;
  h += F("</title><style>"
         "body{font-family:sans-serif;max-width:480px;margin:0 auto;padding:0 16px 40px;"
         "background:#1a1a2e;color:#eee}"
         "h1{color:#4fc3f7;margin-bottom:4px;font-size:20px}"
         "p.sub{color:#90a4ae;font-size:13px;margin:0 0 16px}"
         "nav{display:flex;gap:4px;margin:12px 0 20px}"
         "nav a{flex:1;padding:8px 4px;text-align:center;text-decoration:none;"
         "border-radius:6px;font-size:13px;font-weight:bold;"
         "background:#16213e;color:#90a4ae;border:1px solid #263354}"
         "nav a.active{background:#4fc3f7;color:#1a1a2e;border-color:#4fc3f7}"
         "label{display:block;margin-top:20px;font-weight:bold;color:#90caf9}"
         "select,input[type=text],input[type=number],input[type=url]{"
         "width:100%;padding:9px 8px;margin-top:6px;background:#16213e;color:#eee;"
         "border:1px solid #4fc3f7;border-radius:6px;font-size:15px;box-sizing:border-box}"
         ".rg{margin-top:8px;font-size:14px;color:#b0bec5;display:flex;gap:20px}"
         ".rg label{margin-top:0;font-weight:normal;color:#b0bec5;cursor:pointer}"
         ".lrow{display:flex;gap:10px;margin-top:2px}"
         ".lrow>div{flex:1}"
         ".ni{width:100%;padding:9px 6px;margin-top:4px;background:#16213e;color:#eee;"
         "border:1px solid #78909c;border-radius:6px;font-size:14px;box-sizing:border-box}"
         "small{display:block;color:#90a4ae;font-size:11px;margin-top:10px}"
         ".hint{color:#607d8b;font-size:11px;margin-top:6px}"
         ".btn{margin-top:20px;width:100%;padding:14px;background:#4fc3f7;color:#1a1a2e;"
         "border:none;border-radius:6px;font-size:16px;font-weight:bold;cursor:pointer}"
         ".btn:active{background:#0288d1}"
         ".btn-danger{background:#ef5350;color:#fff}"
         ".btn-danger:active{background:#b71c1c}"
         ".btn-warn{background:#ffa726;color:#1a1a2e}"
         ".btn-warn:active{background:#e65100}"
         ".ok{margin-top:16px;padding:10px 14px;background:#0d4f3c;"
         "border-radius:6px;color:#66bb6a;font-size:14px}"
         ".note{margin-top:28px;padding:10px 14px;background:#1e2a3a;"
         "border-radius:6px;color:#90a4ae;font-size:12px;line-height:1.6}"
         ".card{background:#16213e;border:1px solid #263354;border-radius:8px;"
         "padding:14px 16px;margin-top:16px}"
         ".card h3{margin:0 0 10px;color:#4fc3f7;font-size:15px}"
         ".kv{display:flex;justify-content:space-between;padding:5px 0;"
         "border-bottom:1px solid #263354;font-size:13px}"
         ".kv:last-child{border-bottom:none}"
         ".kv .k{color:#90a4ae}"
         ".kv .v{color:#eee;font-weight:bold;word-break:break-all;text-align:right;max-width:60%}"
         ".sig-good{color:#66bb6a}.sig-fair{color:#ffa726}.sig-poor{color:#ef5350}"
         "hr{border:none;border-top:1px solid #263354;margin:24px 0}"
         "</style></head><body>"
         "<h1>&#x1F4E1; ESP32 Weather</h1>"
         "<p class='sub'>Weather Monitor Web Console</p>"
         "<nav>"
         "<a href='/'");
  if (strcmp(activePage, "settings") == 0) h += F(" class='active'");
  h += F(">&#x2699; 設定</a>"
         "<a href='/dashboard'");
  if (strcmp(activePage, "dashboard") == 0) h += F(" class='active'");
  h += F(">&#x1F4CA; 情報</a>"
         "<a href='/system'");
  if (strcmp(activePage, "system") == 0) h += F(" class='active'");
  h += F(">&#x1F527; システム</a>"
         "</nav>");
  return h;
}

static String buildPageFooter()
{
  return F("</body></html>");
}

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
  String ru     = prefs.getString(KEY_RSS_URL,  DEFAULT_RSS);
  if (ru.length() == 0) ru = DEFAULT_RSS;
  ru.toCharArray(rssUrl, sizeof(rssUrl));
  prefs.end();

  if (myCityIndex < 0 || myCityIndex >= cityCount) myCityIndex = 12;
  if (regionFilter < FILTER_ALL || regionFilter > FILTER_KYUSHU) regionFilter = FILTER_ALL;

  Serial.printf("[Settings] 読込: 都市=%s, 地方=%d, カスタム=%d(%s), RSS=%s\n",
                cities[myCityIndex].name, regionFilter,
                useCustomCity, customCityName, rssUrl);
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
  prefs.putString(KEY_RSS_URL, rssUrl);
  prefs.end();

  Serial.printf("[Settings] 保存: 都市=%s, 地方=%d, カスタム=%d(%s), RSS=%s\n",
                cities[myCityIndex].name, regionFilter,
                useCustomCity, customCityName, rssUrl);
}

// ================================================================
// 自分の都市モードのアクセサ
// useCustomCity かつ SINGLE モードのときカスタム値を返す
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

  String html = buildPageHeader("設定 - ESP32 Weather", "settings");
  html.reserve(html.length() + 4000);

  if (saved) html += F("<div class='ok'>&#x2713; 設定を保存しました</div>");

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
            "<input type='text' name='custom_name'"
            " placeholder='例: 川崎市' maxlength='31' value='");
  html += htmlEscape(customCityName);
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

  // ---- ニュース RSS URL ----
  html += F("<label>ニュース RSS フィード URL</label>"
            "<input type='url' name='rss_url' maxlength='255'"
            " placeholder='https://www3.nhk.or.jp/rss/news/cat0.xml' value='");
  html += htmlEscape(rssUrl);
  html += F("'>"
            "<small class='hint'>NHK: https://www3.nhk.or.jp/rss/news/cat0.xml<br>"
            "BBC: https://feeds.bbci.co.uk/japanese/rss.xml</small>");

  html += F("<button class='btn' type='submit'>保存する</button></form>");

  html += F("<div class='note'>"
            "<b>ボタン操作（短押しのみ）</b><br>"
            "SCREEN &nbsp;→ 天気 &#8644; ニュース<br>"
            "MODE &nbsp;&nbsp;&nbsp;→ 地方巡回 &#8644; 自分の都市<br>"
            "NEXT（天気/巡回中）&nbsp;&nbsp;&nbsp;→ 次の都市<br>"
            "NEXT（天気/自分の都市）→ 週間 &#8644; 毎時<br>"
            "NEXT（ニュース）&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;→ 次の見出し"
            "</div>");

  html += buildPageFooter();
  server.send(200, F("text/html; charset=UTF-8"), html);
}

// ================================================================
// Web サーバー: POST /save — 設定保存
// ================================================================
static void handleSave()
{
  String cityMode = server.hasArg("city_mode") ? server.arg("city_mode") : "preset";
  useCustomCity = (cityMode == "custom");

  if (!useCustomCity) {
    if (server.hasArg("city")) {
      int v = server.arg("city").toInt();
      if (v >= 0 && v < cityCount) myCityIndex = v;
    }
  } else {
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

  if (server.hasArg("region")) {
    int v = server.arg("region").toInt();
    if (v >= FILTER_ALL && v <= FILTER_KYUSHU) regionFilter = v;
  }

  if (server.hasArg("rss_url")) {
    String u = server.arg("rss_url");
    u.trim();
    if (u.length() > 0 && u.length() < RSS_URL_MAX &&
        (u.startsWith("http://") || u.startsWith("https://"))) {
      u.toCharArray(rssUrl, RSS_URL_MAX);
    }
  }

  saveSettings();

  if (currentMode == DisplayMode::SINGLE) {
    if (!useCustomCity) cityIndex = myCityIndex;
  } else {
    if (!cityMatchesFilter(cityIndex)) cityIndex = getFirstCityInRegion();
  }

  prevTimeStr[0] = '\0';
  settingsChanged = true;

  server.sendHeader("Location", "/?saved=1");
  server.send(302);
}

// ================================================================
// Web サーバー: GET /dashboard — 接続情報ダッシュボード
// ================================================================
static void handleDashboard()
{
  String html = buildPageHeader("情報 - ESP32 Weather", "dashboard");
  html.reserve(html.length() + 2400);

  // 自動更新（10秒）
  html += F("<meta http-equiv='refresh' content='10'>");

  // ---- WiFi 情報 ----
  html += F("<div class='card'><h3>&#x1F4F6; WiFi 接続</h3>");

  String ssid = WiFi.SSID();
  html += F("<div class='kv'><span class='k'>SSID</span><span class='v'>");
  html += htmlEscape(ssid.c_str());
  html += F("</span></div>");

  html += F("<div class='kv'><span class='k'>IP アドレス</span><span class='v'>");
  html += WiFi.localIP().toString();
  html += F("</span></div>");

  html += F("<div class='kv'><span class='k'>MAC アドレス</span><span class='v'>");
  html += WiFi.macAddress();
  html += F("</span></div>");

  int rssi = WiFi.RSSI();
  const char* sigClass = rssi >= -60 ? "sig-good" : (rssi >= -75 ? "sig-fair" : "sig-poor");
  const char* sigLabel = rssi >= -60 ? "良好" : (rssi >= -75 ? "普通" : "弱い");
  html += F("<div class='kv'><span class='k'>電波強度 (RSSI)</span>"
            "<span class='v ");
  html += sigClass;
  html += F("'>");
  html += rssi;
  html += F(" dBm (");
  html += sigLabel;
  html += F(")</span></div>");

  html += F("<div class='kv'><span class='k'>チャンネル</span><span class='v'>");
  html += WiFi.channel();
  html += F("</span></div>");

  html += F("</div>");

  // ---- システム情報 ----
  html += F("<div class='card'><h3>&#x1F4BB; システム</h3>");

  unsigned long upSec = millis() / 1000;
  unsigned long d = upSec / 86400;
  unsigned long h = (upSec % 86400) / 3600;
  unsigned long m = (upSec % 3600) / 60;
  unsigned long s = upSec % 60;
  char uptimeBuf[32];
  snprintf(uptimeBuf, sizeof(uptimeBuf), "%lud %02lu:%02lu:%02lu", d, h, m, s);
  html += F("<div class='kv'><span class='k'>稼働時間</span><span class='v'>");
  html += uptimeBuf;
  html += F("</span></div>");

  html += F("<div class='kv'><span class='k'>空きヒープ</span><span class='v'>");
  html += (ESP.getFreeHeap() / 1024);
  html += F(" KB</span></div>");

  html += F("<div class='kv'><span class='k'>最小ヒープ</span><span class='v'>");
  html += (ESP.getMinFreeHeap() / 1024);
  html += F(" KB</span></div>");

  html += F("<div class='kv'><span class='k'>CPU 周波数</span><span class='v'>");
  html += ESP.getCpuFreqMHz();
  html += F(" MHz</span></div>");

  // 現在時刻
  struct tm ti;
  if (getLocalTime(&ti, 0)) {
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y/%m/%d %H:%M:%S", &ti);
    html += F("<div class='kv'><span class='k'>現在時刻 (JST)</span><span class='v'>");
    html += timeBuf;
    html += F("</span></div>");
  }

  html += F("</div>");

  // ---- 現在の設定 ----
  html += F("<div class='card'><h3>&#x2699; 現在の設定</h3>");

  html += F("<div class='kv'><span class='k'>表示モード</span><span class='v'>");
  html += (currentMode == DisplayMode::SINGLE) ? F("自分の都市") : F("地方巡回");
  html += F("</span></div>");

  html += F("<div class='kv'><span class='k'>現在の都市</span><span class='v'>");
  html += htmlEscape(getActiveName());
  html += F("</span></div>");

  if (!useCustomCity) {
    html += F("<div class='kv'><span class='k'>登録都市</span><span class='v'>");
    html += htmlEscape(cities[myCityIndex].name);
    html += F("</span></div>");
  } else {
    char latLon[48];
    snprintf(latLon, sizeof(latLon), "%.4f, %.4f", customCityLat, customCityLon);
    html += F("<div class='kv'><span class='k'>カスタム都市</span><span class='v'>");
    html += htmlEscape(customCityName);
    html += F("</span></div>");
    html += F("<div class='kv'><span class='k'>緯度・経度</span><span class='v'>");
    html += latLon;
    html += F("</span></div>");
  }

  html += F("<div class='kv'><span class='k'>巡回地方</span><span class='v'>");
  html += (regionFilter == FILTER_ALL) ? "全国" : REGION_LABELS[regionFilter];
  html += F("</span></div>");

  html += F("<div class='kv'><span class='k'>RSS フィード</span><span class='v'>");
  html += htmlEscape(rssUrl);
  html += F("</span></div>");

  html += F("</div>");

  html += F("<p style='color:#607d8b;font-size:11px;margin-top:12px;text-align:center'>"
            "10 秒ごとに自動更新</p>");

  html += buildPageFooter();
  server.send(200, F("text/html; charset=UTF-8"), html);
}

// ================================================================
// Web サーバー: GET /system — 再起動・WiFi リセット
// ================================================================
static void handleSystem()
{
  bool restarted  = server.hasArg("restarted");
  bool wifiReset  = server.hasArg("wifireset");

  String html = buildPageHeader("システム - ESP32 Weather", "system");
  html.reserve(html.length() + 1600);

  if (restarted)
    html += F("<div class='ok'>&#x2713; 再起動しました。接続が復帰するまでお待ちください。</div>");
  if (wifiReset)
    html += F("<div class='ok'>&#x2713; WiFi 設定を消去しました。AP モードで起動します。</div>");

  // ---- 再起動 ----
  html += F("<div class='card'><h3>&#x1F504; 再起動</h3>"
            "<p style='color:#b0bec5;font-size:13px;margin:0 0 12px'>"
            "ESP32 を再起動します。WiFi 設定は保持されます。</p>"
            "<form method='POST' action='/restart'>"
            "<button class='btn btn-warn' type='submit'"
            " onclick=\"return confirm('再起動しますか？')\">"
            "&#x1F504; 再起動する</button>"
            "</form></div>");

  // ---- WiFi リセット ----
  html += F("<div class='card'><h3>&#x1F5D1; WiFi 設定を削除</h3>"
            "<p style='color:#b0bec5;font-size:13px;margin:0 0 12px'>"
            "保存済みの WiFi 認証情報を消去して再起動します。<br>"
            "再起動後は AP モード（ESP32_Weather_Config）で起動するので、"
            "スマートフォンで接続して WiFi を再設定してください。</p>"
            "<form method='POST' action='/reset-wifi'>"
            "<button class='btn btn-danger' type='submit'"
            " onclick=\"return confirm('WiFi 設定を削除して再起動しますか？\\n"
            "設定後は再度 WiFi を設定する必要があります。')\">"
            "&#x26A0; WiFi 設定を削除して再起動</button>"
            "</form></div>");

  html += buildPageFooter();
  server.send(200, F("text/html; charset=UTF-8"), html);
}

// ================================================================
// Web サーバー: POST /restart — ESP32 を再起動する
// ================================================================
static void handleRestart()
{
  server.sendHeader("Location", "/system?restarted=1");
  server.send(302);
  delay(300);
  Serial.println(F("[System] Web から再起動要求を受信"));
  ESP.restart();
}

// ================================================================
// Web サーバー: POST /reset-wifi — WiFi 設定削除 + 再起動
// ================================================================
static void handleResetWiFi()
{
  Serial.println(F("[System] Web から WiFi リセット要求を受信"));
  server.sendHeader("Location", "/system?wifireset=1");
  server.send(302);
  delay(300);
  WiFiManager wm;
  wm.resetSettings();
  delay(200);
  ESP.restart();
}

// ================================================================
// Web サーバー初期化・クライアント処理
// ================================================================
void setupWebServer()
{
  server.on("/",           HTTP_GET,  handleRoot);
  server.on("/save",       HTTP_POST, handleSave);
  server.on("/dashboard",  HTTP_GET,  handleDashboard);
  server.on("/system",     HTTP_GET,  handleSystem);
  server.on("/restart",    HTTP_POST, handleRestart);
  server.on("/reset-wifi", HTTP_POST, handleResetWiFi);
  server.begin();
  Serial.printf("[WebServer] 起動: http://%s/\n",
                WiFi.localIP().toString().c_str());
}

void handleWebServer()
{
  server.handleClient();
}
