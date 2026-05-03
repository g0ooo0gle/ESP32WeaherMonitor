/**
 * ESP32 Weather Station - 設定管理 実装
 *
 * [NVS 名前空間]
 *   "wx_cfg"
 *     "myCity"  : 自分の都市インデックス (int)
 *     "region"  : 巡回する地方フィルタ (int, -1=全国)
 *
 * [Web 設定ページ]
 *   GET  /       → 設定フォームを表示
 *   POST /save   → 設定を NVS へ保存し、GET / へリダイレクト
 */

#include "settings.h"
#include "display.h"   // cityIndex（巡回中の都市を調整するため）
#include "button.h"    // currentMode, DisplayMode

#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

// ================================================================
// 設定変数の実体
// ================================================================
int  myCityIndex     = 12;           // デフォルト: 東京
int  regionFilter    = FILTER_ALL;   // デフォルト: 全国
bool settingsChanged = false;        // Web 保存後フラグ

// ================================================================
// 内部変数
// ================================================================
static Preferences prefs;
static WebServer   server(80);

static const char* NVS_NS     = "wx_cfg";
static const char* KEY_CITY   = "myCity";
static const char* KEY_REGION = "region";

// 設定ページ上の地方ラベル（FILTER_HOKKAIDO_TOHOKU〜FILTER_KYUSHU の順）
static const char* REGION_LABELS[] = {
  "北海道・東北地方",
  "関東地方",
  "中部地方",
  "関西地方",
  "中国・四国地方",
  "九州・沖縄地方"
};

// ================================================================
// 設定の読み込み
// ================================================================
void loadSettings()
{
  prefs.begin(NVS_NS, /*readonly=*/true);
  myCityIndex  = prefs.getInt(KEY_CITY,   12);
  regionFilter = prefs.getInt(KEY_REGION, FILTER_ALL);
  prefs.end();

  // 範囲外は初期値に戻す
  if (myCityIndex < 0 || myCityIndex >= cityCount) myCityIndex = 12;
  if (regionFilter < FILTER_ALL || regionFilter > FILTER_KYUSHU) regionFilter = FILTER_ALL;

  Serial.printf("[Settings] 読込: 自分の都市=%s, 地方=%d\n",
                cities[myCityIndex].name, regionFilter);
}

// ================================================================
// 設定の保存
// ================================================================
void saveSettings()
{
  prefs.begin(NVS_NS, /*readonly=*/false);
  prefs.putInt(KEY_CITY,   myCityIndex);
  prefs.putInt(KEY_REGION, regionFilter);
  prefs.end();

  Serial.printf("[Settings] 保存: 自分の都市=%s, 地方=%d\n",
                cities[myCityIndex].name, regionFilter);
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

// ================================================================
// フィルタにマッチする次の都市インデックス
// ================================================================
int getNextCityInRegion(int current)
{
  for (int i = 1; i <= cityCount; i++) {
    int idx = (current + i) % cityCount;
    if (cityMatchesFilter(idx)) return idx;
  }
  return current;  // 一致するものが無ければそのまま
}

// ================================================================
// フィルタにマッチする最初の都市インデックス
// ================================================================
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
  html.reserve(3500);

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

  if (saved) {
    html += F("<div class='ok'>✓ 設定を保存しました</div>");
  }

  // ---- フォーム開始 ----
  html += F("<form method='POST' action='/save'>");

  // ---- 自分の都市 ----
  html += F("<label>自分の都市</label>"
            "<select name='city'>");
  for (int i = 0; i < cityCount; i++) {
    html += F("<option value='");
    html += i;
    html += '\'';
    if (i == myCityIndex) html += F(" selected");
    html += '>';
    html += cities[i].name;
    html += F("</option>");
  }
  html += F("</select>");

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
  if (server.hasArg("city")) {
    int v = server.arg("city").toInt();
    if (v >= 0 && v < cityCount) myCityIndex = v;
  }
  if (server.hasArg("region")) {
    int v = server.arg("region").toInt();
    if (v >= FILTER_ALL && v <= FILTER_KYUSHU) regionFilter = v;
  }

  saveSettings();

  // cityIndex を即時調整（loop() 側で updateWeather() と再描画）
  if (currentMode == DisplayMode::SINGLE) {
    cityIndex = myCityIndex;
  } else {
    if (!cityMatchesFilter(cityIndex)) {
      cityIndex = getFirstCityInRegion();
    }
  }

  settingsChanged = true;   // loop() に天気更新＆再描画を依頼

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
