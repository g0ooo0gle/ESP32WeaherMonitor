/**
 * ESP32 Weather Station - 天気関連機能 実装
 *
 * [このファイルの役割]
 *   - WMO天気コード → 日本語テキスト変換 (getWeatherJp)
 *   - WMO天気コード → 背景色変換         (getBgColor)
 *   - WMO天気コード → アイコン描画        (drawWeatherIcon)
 *   - 週間天気の描画                       (drawWeeklyForecast) ← 新規追加
 *
 * [アイコン対応グループ]
 *   code 0        : 快晴（太陽）
 *   code 1        : 概ね晴れ（太陽＋薄い雲）
 *   code 2〜3     : 曇り（明るい雲・暗い雲）
 *   code 45〜48   : 霧（横線3本）
 *   code 51〜67   : 雨（雲＋雨粒、強さで本数変化）
 *   code 71〜77   : 雪（雲＋雪の結晶）
 *   code 80〜82   : にわか雨（雨と同じ）
 *   code 85〜86   : 雪のシャワー（雪と同じ）
 *   code 95〜99   : 雷雨（黒雲＋稲妻）
 */

#include "weather.h"

// 週間天気データの実体定義（weather.h で extern 宣言済み）
DailyForecast weeklyForecast[WEEKLY_DAYS];
int           weeklyDays = 0;   // 実際に取得できた日数

// ================================================================
// WMO天気コードを日本語テキストに完全変換
// ================================================================
String getWeatherJp(int code)
{
  switch (code) {
  case 0:  return "快晴";
  case 1:  return "概ね晴れ";
  case 2:  return "時々曇り";
  case 3:  return "くもり";
  case 45: return "霧";
  case 48: return "着氷性の霧";
  case 51: return "軽い霧雨";
  case 53: return "霧雨";
  case 55: return "濃い霧雨";
  case 56: return "軽い氷霧雨";
  case 57: return "濃い氷霧雨";
  case 61: return "小雨";
  case 63: return "雨";
  case 65: return "強い雨";
  case 66: return "軽い氷雨";
  case 67: return "強い氷雨";
  case 71: return "小雪";
  case 73: return "雪";
  case 75: return "強い雪";
  case 77: return "粒雪";
  case 80: return "にわか雨(弱)";
  case 81: return "にわか雨";
  case 82: return "激しい雨";
  case 85: return "軽い雪のシャワー";
  case 86: return "激しい雪のシャワー";
  case 95: return "雷雨";
  case 96: return "雷雨と軽い雹";
  case 99: return "雷雨と激しい雹";
  default: return "不明(" + String(code) + ")";
  }
}

// ================================================================
// 天気コードから背景色を判定する関数
// ================================================================
uint16_t getBgColor(int code)
{
  if (code == 0)                                        return COL_BG_CLEAR;
  if (code >= 1  && code <= 3)                          return COL_BG_CLOUDY;
  if (code == 45 || code == 48)                         return COL_BG_FOG;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return COL_BG_RAIN;
  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return COL_BG_SNOW;
  if (code >= 95)                                       return COL_BG_THUNDER;
  return ST77XX_BLACK;
}

// ================================================================
// [内部ヘルパー] 雲を描画する
//   cx, cy : 雲全体の重心座標
//   color  : 雲の色（明るいグレー〜黒雲まで使い分け）
// ================================================================
static void drawCloud(int cx, int cy, uint16_t color)
{
  // 3つの円を横に並べてもこもこした雲を表現します。
  tft.fillCircle(cx - 8, cy + 3, 6, color);   // 左の円
  tft.fillCircle(cx,     cy - 1, 8, color);   // 中央の円（最大・少し上）
  tft.fillCircle(cx + 8, cy + 3, 6, color);   // 右の円

  // 雲の底辺を平らにするために帯を塗りつぶします。
  tft.fillRect(cx - 14, cy + 3, 28, 6, color);
}

// ================================================================
// [内部ヘルパー] 太陽を描画する
//   cx, cy : 太陽の中心座標
// ================================================================
static void drawSun(int cx, int cy)
{
  // 本体（オレンジの円）
  tft.fillCircle(cx, cy, 8, ST77XX_ORANGE);

  // 光線を8方向に描画（45°刻み）
  for (int deg = 0; deg < 360; deg += 45) {
    float rad = deg * DEG_TO_RAD;
    tft.drawLine(
      cx + (int)(cos(rad) * 11), cy + (int)(sin(rad) * 11),
      cx + (int)(cos(rad) * 15), cy + (int)(sin(rad) * 15),
      ST77XX_YELLOW
    );
  }
}

// ================================================================
// [内部ヘルパー] 週間天気用の小さなアイコンを描画（16×12px相当）
//   x, y : アイコン左上座標
//   code : WMO天気コード
// ================================================================
static void drawSmallWeatherIcon(int x, int y, int code)
{
  // 中心座標（アイコン左上から +8, +6 が中心）
  int cx = x + 8;
  int cy = y + 6;

  if (code == 0) {
    // 快晴: 小さな太陽
    tft.fillCircle(cx, cy, 4, ST77XX_ORANGE);
    for (int d = 0; d < 360; d += 45) {
      float r = d * DEG_TO_RAD;
      tft.drawLine(cx + (int)(cos(r)*6), cy + (int)(sin(r)*6),
                   cx + (int)(cos(r)*8), cy + (int)(sin(r)*8), ST77XX_YELLOW);
    }
  }
  else if (code == 1) {
    // 概ね晴れ: 小太陽＋小雲
    tft.fillCircle(cx - 2, cy - 1, 3, ST77XX_ORANGE);
    tft.fillCircle(cx + 2, cy + 2, 4, 0xC618);
    tft.fillRect(cx - 2, cy + 2, 8, 3, 0xC618);
  }
  else if (code == 2 || code == 3) {
    // 曇り: グレーの雲
    uint16_t c = (code == 2) ? 0xC618 : 0x8410;
    tft.fillCircle(cx - 3, cy + 1, 3, c);
    tft.fillCircle(cx + 2, cy - 1, 4, c);
    tft.fillCircle(cx + 6, cy + 1, 3, c);
    tft.fillRect(cx - 6, cy + 1, 12, 3, c);
  }
  else if (code == 45 || code == 48) {
    // 霧: 短い横線2本
    tft.drawFastHLine(x + 2, cy - 1, 12, 0x8410);
    tft.drawFastHLine(x + 2, cy + 3, 12, 0x8410);
  }
  else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    // 雨: 小雲＋雨粒
    tft.fillCircle(cx, cy - 2, 4, 0x8410);
    tft.fillRect(cx - 4, cy - 2, 8, 3, 0x8410);
    int drops = (code == 51 || code == 61 || code == 80) ? 2 : 3;
    for (int i = 0; i < drops; i++) {
      tft.drawLine(x + 4 + i*4, cy + 3, x + 3 + i*4, cy + 7, ST77XX_CYAN);
    }
  }
  else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
    // 雪: 小雲＋小さな点（雪片）
    tft.fillCircle(cx, cy - 2, 4, 0x9CF3);
    tft.fillRect(cx - 4, cy - 2, 8, 3, 0x9CF3);
    // 雪片2個
    for (int i = 0; i < 2; i++) {
      int sx = x + 3 + i * 7;
      int sy = cy + 5;
      tft.drawLine(sx, sy - 2, sx, sy + 2, ST77XX_WHITE);
      tft.drawLine(sx - 2, sy, sx + 2, sy, ST77XX_WHITE);
    }
  }
  else if (code >= 95) {
    // 雷: 黒雲＋小稲妻
    tft.fillCircle(cx, cy - 2, 4, 0x4208);
    tft.fillRect(cx - 4, cy - 2, 8, 3, 0x4208);
    tft.fillTriangle(cx + 1, cy + 2, cx - 2, cy + 6, cx + 1, cy + 6, ST77XX_YELLOW);
    tft.fillTriangle(cx,     cy + 5, cx + 3, cy + 5, cx - 1, cy + 9, ST77XX_YELLOW);
  }
  else {
    // 不明
    tft.drawPixel(cx, cy, ST77XX_WHITE);
  }
}

// ================================================================
// 天気アイコン描画関数（36×36px）
// ================================================================
void drawWeatherIcon(int x, int y, int code)
{
  if (code == 0) {
    drawSun(x + 18, y + 18);
  }
  else if (code == 1) {
    drawSun(x + 12, y + 13);
    drawCloud(x + 20, y + 24, 0xC618);
  }
  else if (code == 2) {
    drawCloud(x + 18, y + 20, 0xC618);
  }
  else if (code == 3) {
    drawCloud(x + 18, y + 20, 0x8410);
  }
  else if (code == 45 || code == 48) {
    // 霧: 横線3本
    for (int i = 0; i < 3; i++)
      tft.drawRoundRect(x + 4, y + 12 + i * 8, 28, 3, 1, 0x8410);
  }
  else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    drawCloud(x + 18, y + 14, 0x8410);
    int drops;
    if      (code == 51 || code == 61 || code == 80) drops = 2;
    else if (code == 53 || code == 63 || code == 81) drops = 3;
    else                                              drops = 4;
    for (int i = 0; i < drops; i++) {
      int rx = x + 8 + i * 7;
      tft.drawLine(rx, y + 24, rx - 2, y + 31, ST77XX_CYAN);
    }
  }
  else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
    drawCloud(x + 18, y + 14, 0x9CF3);
    int snowX[] = {x + 9, x + 18, x + 27};
    for (int i = 0; i < 3; i++) {
      int sx = snowX[i], sy = y + 29;
      tft.drawLine(sx, sy - 3, sx, sy + 3, ST77XX_WHITE);
      tft.drawLine(sx - 3, sy, sx + 3, sy, ST77XX_WHITE);
      tft.drawLine(sx - 2, sy - 2, sx + 2, sy + 2, 0xB5B6);
      tft.drawLine(sx + 2, sy - 2, sx - 2, sy + 2, 0xB5B6);
    }
  }
  else if (code >= 95) {
    drawCloud(x + 18, y + 14, 0x4208);
    tft.fillTriangle(x + 21, y + 20, x + 14, y + 29, x + 20, y + 29, ST77XX_YELLOW);
    tft.fillTriangle(x + 19, y + 27, x + 24, y + 27, x + 15, y + 36, ST77XX_YELLOW);
  }
  else {
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.setCursor(x + 13, y + 12);
    tft.print("?");
  }
}

// ================================================================
// 週間天気を AREA_WEEKLY エリアに描画する
//
// [レイアウト（1行 = 10px）]
//   各行: [小アイコン16px] [ラベル "今日"等] [最高℃] [/] [最低℃]
//   6日分を縦に並べます（1行あたり10px）
// ================================================================
void drawWeeklyForecast()
{
  if (weeklyDays == 0) return;   // データ未取得なら何もしない

  // エリア全体を背景色（黒）で塗りつぶしてから描画
  tft.fillRect(0, AREA_WEEKLY_Y, SCREEN_W, AREA_WEEKLY_H, ST77XX_BLACK);

  // 1行の高さ（AREA_WEEKLY_H / WEEKLY_DAYS = 60/6 = 10px）
  const int rowH = AREA_WEEKLY_H / WEEKLY_DAYS;

  for (int i = 0; i < weeklyDays && i < WEEKLY_DAYS; i++)
  {
    int rowY = AREA_WEEKLY_Y + i * rowH;   // この行のY座標

    // ---- 小アイコン（16×12px、行の左端）----
    // アイコンの縦中央を行の中央に合わせます（rowH=10なので rowY+0 が中央寄り）
    drawSmallWeatherIcon(2, rowY, weeklyForecast[i].weatherCode);

    // ---- ラベル（"今日" / "明日" / "月" など）----
    // setTextSize(1) = 6×8px の標準フォント
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(20, rowY + 2);
    tft.print(weeklyForecast[i].label);

    // ---- 最高気温（赤）----
    // snprintf で小数点なし整数に変換（スペース節約）
    char buf[8];
    tft.setTextColor(0xF800);   // 赤（RGB565: R=31, G=0, B=0）
    tft.setCursor(46, rowY + 2);
    snprintf(buf, sizeof(buf), "%3.0f", weeklyForecast[i].tempMax);
    tft.print(buf);

    // ---- 区切りスラッシュ（白）----
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(70, rowY + 2);
    tft.print("/");

    // ---- 最低気温（水色）----
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(78, rowY + 2);
    snprintf(buf, sizeof(buf), "%3.0f", weeklyForecast[i].tempMin);
    tft.print(buf);

    // ---- 単位 "℃"（薄い文字で省スペース）----
    tft.setTextColor(0x8410);   // 暗いグレー
    tft.setCursor(102, rowY + 2);
    tft.print("C");

    // 行の下に薄い区切り線（最終行は除く）
    if (i < weeklyDays - 1)
      tft.drawFastHLine(18, rowY + rowH - 1, SCREEN_W - 18, 0x2104);
  }
}
