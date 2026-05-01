/**
 * ESP32 Weather Station - 天気関連機能 実装
 */

#include "weather.h"

// ================================================================
// WMO天気コードを日本語テキストに完全変換
// Open-Meteoが採用している国際基準のコード表（0〜99）をすべて網羅。
// ================================================================
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

// ================================================================
// 天気コードから背景色を判定する関数
// 天気グループごとに背景色を返しています。
// ================================================================
uint16_t getBgColor(int code)
{
  if (code == 0)
    return COL_BG_CLEAR;    // 快晴
  if (code >= 1 && code <= 3)
    return COL_BG_CLOUDY;   // 晴れ・曇り
  if (code == 45 || code == 48)
    return COL_BG_FOG;      // 霧
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82))
    return COL_BG_RAIN;     // 雨
  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86))
    return COL_BG_SNOW;     // 雪
  if (code >= 95)
    return COL_BG_THUNDER;  // 雷
  return ST77XX_BLACK;      // 不明なコードは黒で表示
}

// ================================================================
// 天気アイコン描画関数（図形プリミティブによる描画）
// デザイン案に基づき、36x36pxの範囲内で各天気を表すアイコンを描画。
// Adafruit_GFXの描画関数だけで完結させて、外部データ（画像ファイル等）を
// 依存させないようにしています（組み込み向けの実装方針です）。
// ================================================================
void drawWeatherIcon(int x, int y, int code)
{
  if (code == 0)
  {
    // 快晴: 太陽
    tft.fillCircle(x + 18, y + 18, 10, ST77XX_ORANGE);
    for (int i = 0; i < 360; i += 45)
    {
      float rad = i * DEG_TO_RAD;
      tft.drawLine(x + 18 + cos(rad) * 12, y + 18 + sin(rad) * 12,
                   x + 18 + cos(rad) * 16, y + 18 + sin(rad) * 16, ST77XX_YELLOW);
    }
  }
  else if (code >= 1 && code <= 3)
  {
    // 曇り系（円を複数重ねて雲を表現）
    tft.fillCircle(x + 14, y + 22, 7, 0x8410);   // 暗いグレー
    tft.fillCircle(x + 22, y + 18, 9, 0xAD55);   // 明るいグレー
    tft.fillCircle(x + 28, y + 22, 6, 0x8410);
  }
  else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82))
  {
    // 雨系（雲 + 斜めの雨粒の線）
    tft.fillCircle(x + 18, y + 15, 8, 0x8410);   // 雲
    for (int i = 0; i < 3; i++)                    // 雨粒（斜め線）
      tft.drawLine(x + 12 + i * 6, y + 25, x + 10 + i * 6, y + 32, ST77XX_CYAN);
  }
  else if (code >= 95)
  {
    // 雷系（黒雲 + ライitingの三角形2つ）
    tft.fillCircle(x + 18, y + 15, 8, 0x4208);   // 黒雲
    tft.fillTriangle(x + 20, y + 20, x + 14, y + 29, x + 19, y + 29, ST77XX_YELLOW);
    tft.fillTriangle(x + 18, y + 27, x + 23, y + 27, x + 16, y + 36, ST77XX_YELLOW);
  }
  else
  {
    // 霧・雪など（横線を3本並べて表現）
    for (int i = 0; i < 3; i++)
      tft.drawRoundRect(x + 4, y + 12 + i * 8, 28, 3, 1, ST77XX_WHITE);
  }
}
