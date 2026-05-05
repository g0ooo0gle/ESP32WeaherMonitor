/**
 * cities.h - 都市データ定義ヘッダ
 *
 * 47 都道府県庁所在地の緯度・経度・地方ブロックを管理します。
 *
 * [地方ブロック一覧]
 *   REGION_HOKKAIDO  : 北海道（1 都市）
 *   REGION_TOHOKU    : 東北（6 都市）
 *   REGION_KANTO     : 関東（7 都市）
 *   REGION_CHUBU     : 中部（9 都市）※北陸・甲信越・東海を含む
 *   REGION_KINKI     : 近畿（7 都市）
 *   REGION_CHUGOKU   : 中国（5 都市）
 *   REGION_SHIKOKU   : 四国（4 都市）
 *   REGION_KYUSHU    : 九州（8 都市）
 *   REGION_OKINAWA   : 沖縄（1 都市）
 */

#ifndef CITIES_H
#define CITIES_H

// ------------------------------------------------------------------
// 地方ブロックの列挙型
// ------------------------------------------------------------------
enum RegionBlock {
  REGION_HOKKAIDO = 0,
  REGION_TOHOKU,
  REGION_KANTO,
  REGION_CHUBU,
  REGION_KINKI,
  REGION_CHUGOKU,
  REGION_SHIKOKU,
  REGION_KYUSHU,
  REGION_OKINAWA
};

extern const char* regionNames[];

// ------------------------------------------------------------------
// 都市データ構造体
// ------------------------------------------------------------------
struct CityData {
  const char  *name;
  float        lat;
  float        lon;
  RegionBlock  region;
};

// ------------------------------------------------------------------
// 都市リスト（47 都道府県庁所在地）
// ------------------------------------------------------------------
extern const CityData cities[];
extern const int cityCount;

#endif // CITIES_H
