/**
 * ESP32 + ST7735S 1.8inch Display Project
 * 
 * [接続メモ]
 * VCC    -> 3.3V
 * GND    -> GND
 * CS     -> GPIO 5
 * RESET  -> GPIO 4
 * D/C(A0)-> GPIO 2
 * SDA    -> GPIO 23
 * SCLK   -> GPIO 18
 * BL(LED)-> 3.3V (バックライト)
 * 
 */

#include <Arduino.h>
#include <Adafruit_GFX.h>    // コア描画ライブラリ
#include <Adafruit_ST7735.h> // ST7735専用ライブラリ
#include <U8g2_for_Adafruit_GFX.h> // 日本語用ライブラリ
#include <SPI.h>

// --- ピン配置の定義 ---
// ESP32の標準SPI（VSPI）ピンを使用
#define TFT_SCLK 18  // SCL (Clock)
#define TFT_MOSI 23  // SDA (Data)
#define TFT_RST   4  // RESET
#define TFT_DC    2  // A0 (Data/Command)
#define TFT_CS    5  // CS (Chip Select)

// インスタンスの作成
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2; // 日本語フォント用インスタンス

void setup() {
  Serial.begin(115200);
  Serial.println("ST7735 Test Start...");

  // 1. ディスプレイの初期化
  // ST7735S 1.8インチモデルの初期化
  // 黒タブ（INITR_BLACKTAB）が一般的ですが、
  // 画面端がズレたり色がおかしい場合は INITR_GREENTAB などを試してください
  tft.initR(INITR_BLACKTAB);

  // 画面の向き（0-3） 1は横向き
  tft.setRotation(1);

  // 画面全体を黒で塗りつぶし（初期化）
  tft.fillScreen(ST77XX_BLACK);

  // 2. 日本語ライブラリをAdafruit GFXに紐付ける
  u8g2.begin(tft);

  // --- 描画開始 ---

  // 英語（Adafruit GFX標準）
  // テキストの設定と表示
  tft.setCursor(0, 0);           // 表示開始位置 (x, y)
  tft.setTextColor(ST77XX_YELLOW); // 文字色：黄色
  tft.setTextSize(2);              // 文字サイズ：2（中）、 3（かなり大きめ）
  tft.print("TEST OK!");

  // 日本語（U8g2）
  // フォント設定（bはボールド、12/14/16などはピクセルサイズ）
  u8g2.setFont(u8g2_font_unifont_t_japanese1); // 日本語基本フォント
  u8g2.setFontMode(1);                          // 背景を透明にする
  u8g2.setFontDirection(0);                     // 文字の向き（0:横）
  u8g2.setForegroundColor(ST77XX_WHITE);        // 
  
  // U8g2の描画（x, y は左下の位置になるので注意）
  u8g2.setCursor(10, 35);
  u8g2.print("日本語テスト");

  u8g2.setCursor(10, 90);
  u8g2.setForegroundColor(ST77XX_CYAN);
  u8g2.print("ESP32で表示中");

  // 下の方に補足文字
  tft.setCursor(20, 100);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.print("ST7735S Initialized.");
  
  Serial.println("Display Done.");
}

void loop() {
  // 文字を点滅させる簡単なデモ
  delay(1000);
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_BLACK); // 背景色で上書きして消す
  tft.setTextSize(2);
  tft.print("TEST OK!");

  delay(500);
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print("TEST OK!");

  // 動いている確認用の点滅処理
  delay(1000);
  u8g2.setCursor(10, 120);
  u8g2.setForegroundColor(ST77XX_MAGENTA);
  u8g2.print("READY >");
  
  delay(1000);
  tft.fillRect(10, 110, 100, 20, ST77XX_BLACK); // 特定範囲を黒塗りで消去
}