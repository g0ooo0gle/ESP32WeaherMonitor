#include <Arduino.h>
#include <Adafruit_GFX.h>    // コア描画ライブラリ
#include <Adafruit_ST7735.h> // ST7735専用ライブラリ
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

void setup() {
  Serial.begin(115200);
  Serial.println("ST7735 Test Start...");

  // ST7735S 1.8インチモデルの初期化
  // 黒タブ（INITR_BLACKTAB）が一般的ですが、
  // 画面端がズレたり色がおかしい場合は INITR_GREENTAB などを試してください
  tft.initR(INITR_BLACKTAB);

  // 画面の向き（0-3） 1は横向き
  tft.setRotation(1);

  // 画面全体を黒で塗りつぶし（初期化）
  tft.fillScreen(ST77XX_BLACK);

  // テキストの設定と表示
  tft.setCursor(20, 50);           // 表示開始位置 (x, y)
  tft.setTextColor(ST77XX_YELLOW); // 文字色：黄色
  tft.setTextSize(3);              // 文字サイズ：3（かなり大きめ）
  tft.print("TEST");

  // 下の方に補足文字
  tft.setCursor(20, 90);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.print("ST7735S Initialized.");
  
  Serial.println("Display Done.");
}

void loop() {
  // 文字を点滅させる簡単なデモ
  delay(1000);
  tft.setCursor(20, 50);
  tft.setTextColor(ST77XX_BLACK); // 背景色で上書きして消す
  tft.setTextSize(3);
  tft.print("TEST");

  delay(500);
  tft.setCursor(20, 50);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print("TEST");
}