/**
 * ESP32 Weather Station - 都市データ
 *
 * [変更なし]
 *   今回のリファクタでは cities は変更していません。
 *   日本全47都道府県庁所在地を引き続き使用します。
 *
 * [地方ブロック一覧]
 *   REGION_HOKKAIDO  : 北海道（1都市）
 *   REGION_TOHOKU    : 東北（6都市）
 *   REGION_KANTO     : 関東（7都市）
 *   REGION_CHUBU     : 中部（9都市）※北陸・甲信越・東海を含む
 *   REGION_KINKI     : 近畿（7都市）
 *   REGION_CHUGOKU   : 中国（5都市）
 *   REGION_SHIKOKU   : 四国（4都市）
 *   REGION_KYUSHU    : 九州（8都市）
 *   REGION_OKINAWA   : 沖縄（1都市）
 */

#ifndef CITIES_H
#define CITIES_H

// ------------------------------------------------------------------
// 地方ブロックの列挙型
// uint8_t (0〜255) で表現し、メモリ消費を最小限にしています。
// ------------------------------------------------------------------
enum RegionBlock {
  REGION_HOKKAIDO = 0,   // 北海道
  REGION_TOHOKU,         // 東北
  REGION_KANTO,          // 関東
  REGION_CHUBU,          // 中部（北陸・甲信越・東海を含む）
  REGION_KINKI,          // 近畿
  REGION_CHUGOKU,        // 中国
  REGION_SHIKOKU,        // 四国
  REGION_KYUSHU,         // 九州
  REGION_OKINAWA         // 沖縄
};

// 地方ブロック名の文字列テーブル（RegionBlock の値をインデックスとして使用）
extern const char* regionNames[];

// ------------------------------------------------------------------
// 都市データ構造体
// ------------------------------------------------------------------
struct CityData {
  const char   *name;    // 都市の名前（日本語）
  float         lat;     // 緯度
  float         lon;     // 経度
  RegionBlock   region;  // 地方ブロック
};

// ------------------------------------------------------------------
// 都市リスト（47都道府県庁所在地）
// ------------------------------------------------------------------
extern const CityData cities[];
extern const int cityCount;

#endif // CITIES_H
