/*
  zundabreak2.ino
  30分以上PCに向かっているとずんだもんが休憩を促すデバイス for M5Stack ATOMS3
  ToFセンサー Unit-ToF4M使用

  Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT

  CREDIT
    画像データ 坂本アヒルさん
    キャラクター　東北ずん子ずんだもんプロジェクト
    音声データ VOICEVOX: ずんだもん

  ※本プログラムはMITライセンスですが（個別に記載があるものを除く）、
  音声データ、画像データ、およびキャラクターに関しては、それぞれの
  ライセンス・利用規約が適用されます。
*/
/*
TODO:
  Ticker once_msでも良くない？
  deep sleepに対応したい　参考 https://qiita.com/yomori/items/ef20d888a79aaedb58fe
*/
#include <M5Unified.h>

// 基本動作設定
const uint32_t WORK_LIMIT = 1800;       // 30分以上働いたら休憩を促す(s)
const uint32_t BREAK_TIME_LONG = 90;    // 休憩の長さ(s)
const int DISPLAY_DISTANCE_MIN = 250;   // PCの前にいると判断する位置 MIN (mm)
const int DISPLAY_DISTANCE_MAX = 750;   // PCの前にいると判断する位置 MAX (mm)

// その他動作設定
const int TIRED_LIMIT_1 = 85;   // 疲労レベル閾値1 (0-100)  85=約25分後
const int TIRED_LIMIT_2 = 95;   // 疲労レベル閾値2 (0-100)  95=約28分後
const bool SHOW_DISTANCE_GAUGE = true;  // 距離確認用のゲージを常に表示する
const bool SHOW_DISTANCE_AHIRU = true;  // 距離確認用のアヒルちゃんを表示する
const uint32_t BLINK_TIME_BASE = 2000;  // まばたき時間　基本間隔
const uint32_t BLINK_TIME_RAND = 2000;  // まばたき時間　ランダム追加 max
const uint32_t BLINK_TIME_CLOS = 150;   // まばたき時間　閉じてる時間
const uint32_t KYORO_TIME_BASE = 1500;  // キョロキョロ時間　基本間隔
const uint32_t KYORO_TIME_RMAX = 2000;  // キョロキョロ時間　ランダム追加 max

// GPIO設定 ATOMS3
#define GPIO_SDA  2   // Grove SDA
#define GPIO_SCL  1   // Grove SCL
#define GPIO_I2S_BCLK  5    // I2S BCLK (Atomic SPK Base compatible)
#define GPIO_I2S_LRCK  39   // I2S LRCK (Atomic SPK Base compatible)
#define GPIO_I2S_DATA  38   // I2S DATA (Atomic SPK Base compatible)

// アバター関連
#include "Zundavatar.h"   // アバタークラス
using namespace zundavatar;
bool lcdBusy = false;   // ディスプレイの排他制御用　クラス間で共有される
Zundavatar avatar(&lcdBusy);

// アバター画像データ
#include "data_image_zundamon128.h"   // 画像データ　ずんだもん

// オーディオ出力関連
#include <AudioFileSourcePROGMEM.h>
#include <AudioGeneratorWAV.h>
#include <AudioOutputI2S.h>
AudioGeneratorWAV *wav;
AudioFileSourcePROGMEM *file;
AudioOutputI2S *out;

// 音声データ
#include "data_sound_zundamon.h"   // 音声データ　ずんだもん
const int SAMPLE_RATE = 24000;
const unsigned char* soundData[] = { sound000, sound001, sound002 };
const size_t sizeData[] = { sizeof(sound000), sizeof(sound001), sizeof(sound002) };

// ToF距離センサー関係 Unit-ToF4M使用
#include <Wire.h>
#include <VL53L1X.h>
VL53L1X tof;

// 割り込み処理
#include <Ticker.h>
Ticker tickerk;
volatile bool refresh;

// グローバル変数
M5Canvas tgauge, tinfo;
M5Canvas red, blue, green, purple;
bool blink = false;
uint16_t blinkOpen = 1;
bool kyoro = false;
bool tofok = false;

// デバッグに便利なマクロ定義 -- M5.Log置き換えver --------
// 参考ページ https://github.com/lovyan03/M5Unified_HandsOn/blob/main/sample_code/Log1.cpp
#define sp(x) M5.Log.println(String(x).c_str())
#define spn(x) M5.Log.print(String(x).c_str())
#define spp(k,v) M5.Log.println(String(String(k)+"="+String(v)).c_str())
#define spf(fmt, ...) M5.Log.printf(fmt, __VA_ARGS__)
#define array_length(x) (sizeof(x) / sizeof(x[0]))


// ====================================================================================

// 体力ゲージのイメージを作成する
void drawHpGage(M5Canvas* canvas, int tired) {
  int c;
  int w = canvas->width();
  int h = canvas->height();
  canvas->fillSprite(0x0020);   // 透明色で塗りつぶし
  c = 360 * ((float)(100 - tired) / 100.0);
  canvas->fillArc(w/2,h/2, 5,9, 0,c, TFT_DARKGREEN);
  if (c > 0) canvas->fillArc(w/2,h/2, 5,9, c,360, TFT_RED);
}

// 距離ゲージのイメージを作成する
void drawDistanceGage(M5Canvas* canvas, int now, int limitLow, int limitHigh, int maxDepth=4000) {
  int y;
  int w = canvas->width();
  int h = canvas->height();
  if (now < 0) now = 0;
  if (now > maxDepth) now = maxDepth;
  //canvas->fillSprite(TFT_WHITE);   // 背景色で塗りつぶし
  canvas->fillSprite(0x0020);   // 透明色で塗りつぶし
  y = (h-1) - (h-1) * ((float)limitLow / maxDepth);
  canvas->drawLine(w/2,y, w-1,y, TFT_DARKGREEN);
  y = (h-1) - (h-1) * ((float)limitHigh / maxDepth);
  canvas->drawLine(w/2,y, w-1,y, TFT_DARKGREEN);
  y = (h-1) - (h-1) * ((float)now / maxDepth);
  canvas->drawLine(0,y, w-1,y, TFT_RED);
}

// デバッグモード：矢印付きゲージのイメージを作成する
void drawArrowGage(M5Canvas* canvas, int now, int limitLow, int limitHigh, int maxDepth=4000) {
  int x, y, x1, x2;
  int w = canvas->width();
  int h = canvas->height();
  if (now < 0) now = 0;
  if (now > maxDepth) now = maxDepth;
  canvas->fillSprite(0x0020);   // 透明色で塗りつぶし
  canvas->drawRect(1,9, w-2, 12, TFT_BLACK);
  canvas->fillRect(2,10, w-4, 10, TFT_SKYBLUE);
  x1 = (w-4) * ((float)limitLow / maxDepth);
  x2 = (w-4) * ((float)limitHigh / maxDepth);
  canvas->fillRect(x1+2,10, (x2-x1), 10, TFT_GREEN);
  x = (w-4) * ((float)now / maxDepth);
  y = 8;
  canvas->fillRect(x-1,1, 3,h-1, TFT_RED);
  canvas->fillTriangle(x,y, x-6,y-7, x+6, y-7, TFT_RED);
}

// デバッグモード：距離メーターのイメー時を作成する
void drawDebugDistance(M5Canvas* canvas, int distance, bool fuchi=false) {
  int w = canvas->width();
  int x, y;
  canvas->fillSprite(0x0020);   // 透明色で塗りつぶし
  canvas->setFont(&fonts::Font4); // 14x26
  canvas->setTextDatum(textdatum_t::top_right);
  String text = String(distance / 10) + "cm";
  x = w - 2;
  y = 2;
  if (fuchi) {
    canvas->setTextColor(TFT_WHITE);
    for (int i=-1; i<=1; i++) {
      for (int j=-1; j<=1; j++) {
        canvas->drawString(text, x+i, y+j);
      }
    }
  }
  canvas->setTextColor(TFT_BLACK);
  canvas->drawString(text, x, y);
}

// デバッグモード：ToFセンサー距離調整をするためのモード
void debugmode() {
  // ゲージと距離表示のcanvasを作成
  tgauge.setColorDepth(16);
  tgauge.createSprite(128, 20);
  avatar.setInsertedLayer(0, OVR_FRONT_AVATER, &tgauge, 0,108);   // ゲージのcanvasをアバターに挿入設定
  tinfo.setColorDepth(16);
  tinfo.createSprite(80, 30);
  avatar.setInsertedLayer(1, OVR_FRONT_AVATER, &tinfo, 128-80,0);   // 距離メーターのcanvasをアバターに挿入設定
  while (1) {
    M5.update();
    // 距離を測定
    int distance = tof.readRangeContinuousMillimeters();
    spp("distance",distance);
    // 距離に応じてずんだもんの表情を変える
    defaultAvatarParts();
    if (distance < DISPLAY_DISTANCE_MIN) {  // 近すぎる場合
      avatar.changeParts("eye", 13);     // 目　ジト目
      avatar.changeParts("mouth", 11);   // 口　△
      avatar.changeParts("rhand", 3);   // 右腕　口元
    } else if (distance > DISPLAY_DISTANCE_MAX) { // 遠すぎる場合
      avatar.changeParts("body", 1);    // 体　萎え
      avatar.changeParts("eye", 14);     // 目　なごみ
    } else {  // 範囲内の場合
      avatar.changeParts("eye", 1);     // 目　普通　開く（中）
      avatar.changeParts("mouth", 1);   // 口　普通　（ほあー）
      avatar.changeParts("rhand", 2);   // 右腕　上げ
      avatar.changeParts("lhand", 2);   // 左腕　上げ
    }
    drawArrowGage(&tgauge, distance, DISPLAY_DISTANCE_MIN, DISPLAY_DISTANCE_MAX, 1500); // ゲージを作成
    drawDebugDistance(&tinfo, distance, true);  // 距離メーターを作成
    avatar.drawAvatar(); // アバター全体表示
    delay(100);
  }
}

// 配列の中に指定した値があるか？
// bool inArray(uint16_t value, const uint16_t* array, size_t size) {
//   for (size_t i = 0; i < size; i++) {
//     if (array[i] == value) return true;
//   }
//   return false;
// }

// アバターのパーツをデフォルトに設定する
void defaultAvatarParts() {
  avatar.changeParts("body", 0);    // 体
  avatar.changeParts("rhand", 0);   // 右腕
  avatar.changeParts("lhand", 0);   // 左腕
  avatar.changeParts("eyebrow", 0); // 眉毛
  avatar.changeParts("eye", 1);     // 目
  avatar.changeParts("mouth", 0);   // 口
  avatar.changeParts("ahiru", 0);   // アヒル
}

// 指定時間待機する。待機中もM5.update()を行っており、ボタンが押されたら即座に抜ける
bool waitMillis(uint32_t ms) {
  bool res = false;
  uint32_t tmwait = millis() + ms;
  while (millis() < tmwait) {
    M5.update();
    if (M5.BtnA.isPressed()) {
      res = true;
      break;
    }
    delay(10);
  }
  return res;
}

// 喋ると同時にリップシンクを行う
void playVoice(int no, bool withlip) {  // Default: withlip=false
  const int lipsyncDuration = 200;  // リップシンクの間隔(ms)
  unsigned long stams = millis();
  unsigned long lastms = 0;

  // 再生開始
  file = new AudioFileSourcePROGMEM(soundData[no], sizeData[no]);
  wav = new AudioGeneratorWAV();
  wav->begin(file, out);
  while (wav->isRunning()) {
    wav->loop();

    // リップシンク
    if (withlip && (millis() - lastms) >= lipsyncDuration) {
      // 平均音量を計算
      int idx = ((millis() - stams) * SAMPLE_RATE) / 1000 * 2;
      int numSamples = (lipsyncDuration * SAMPLE_RATE) / 1000;
      int totalSamples = 0;
      long totalVolume = 0;
      for (int i = 0; i < numSamples; i++) {
        if ((idx + i*2 + 1) >= sizeData[no]) break;
        int16_t val = soundData[no][idx + i*2] | (soundData[no][idx + i*2 + 1] << 8);
        totalVolume += abs(val);
        totalSamples++;
      }
      int averageVolume = (totalSamples > 0) ? totalVolume / totalSamples : 0;
      // 音の大きさに合わせてアバターの口を変更（閾値は要調整）
      if (averageVolume > 1000)     avatar.setLipsyncVowel(5, 80);  // "o" 80ms  口　お　（お）
      else if (averageVolume > 100) avatar.setLipsyncVowel(2, 80);  // "a" 80ms　口　あ　（ほあー）
      else                          avatar.setLipsyncVowel(0, 1);   // "n" 1ms
      lastms = millis();
    }
    delay(5);
  }

  // 再生終了
  wav->stop();
  if (withlip) avatar.setLipsyncVowel(0, 1);
}

// Tiker処理：自動的にキョロキョロする（描画はせず、refreshフラグを立てるのみ）
uint16_t kyoroEyeSelect = 0;  // キョロキョロパターンの選択
static constexpr uint16_t kyoroEyeNum = 4;      // キョロキョロする目の数
const uint8_t kyoroEyePattern[3][kyoroEyeNum] = {  // キョロキョロする目のインデックス番号：目　普通
  { 3,1,2,1 }, { 6,4,5,4 }, { 9,7,8,7 }   // 元気なとき、少し疲れたとき、かなり疲れたとき
};
void startKyorokyoro() {
  if (tickerk.active()) return;
  uint32_t kyoroms = KYORO_TIME_BASE + random(0, KYORO_TIME_RMAX);
  tickerk.attach_ms(kyoroms, tickerKyorokyoro);
}
void tickerKyorokyoro() {
  static uint16_t pat = 0;
  int krrand = random(0,3);// pat++ % kyoroEyeNum
  if (blink) {
    avatar.setBlink("eye", kyoroEyePattern[kyoroEyeSelect][krrand], 0);   // まばたきの目の設定を変更　注意：変更すると即座に反映されるのでrefreshは不要
  } else {
    avatar.changeParts("eye", kyoroEyePattern[kyoroEyeSelect][krrand]);   // 目を変更
    refresh = true;
  }
  // 次回実行間隔をランダムに変える
  if (tickerk.active()) {
    uint32_t kyoroms = KYORO_TIME_BASE + random(0, KYORO_TIME_RMAX);
    tickerk.detach();
    tickerk.attach_ms(kyoroms, tickerKyorokyoro);
  }
}
void stopKyorokyoro() {
  if (!tickerk.active()) return;
  tickerk.detach();
}

// 空きメモリ確認
void debug_free_memory(String str) {
  sp("## "+str);
  spf("heap_caps_get_free_size(MALLOC_CAP_DMA):%d\n", heap_caps_get_free_size(MALLOC_CAP_DMA) );
  spf("heap_caps_get_largest_free_block(MALLOC_CAP_DMA):%d\n", heap_caps_get_largest_free_block(MALLOC_CAP_DMA) );
  spf("heap_caps_get_free_size(MALLOC_CAP_SPIRAM):%d\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) );
  spf("heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM):%d\n\n", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) );
}

// ====================================================================================

// 初期化
void setup() {
  // 初期化
  auto cfg = M5.config();
  // cfg.external_spk = true;  // 別に設定しなくても変わらなかったから意味ないかも??
  // cfg.serial_baudrate = 115200;
  M5.begin(cfg); 
  Wire.begin(GPIO_SDA, GPIO_SCL);
  delay(1000);
  sp("Start!");
  debug_free_memory("Start");

  // ログレベルの設定
  // M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_INFO); // 表示するログレベル ERROR WARN INFO DEBUG VERBOSE
  // M5.Log.setEnableColor(m5::log_target_serial, false);  // ログカラー

  // スピーカーI2S出力設定
  out = new AudioOutputI2S();
  out->SetPinout(GPIO_I2S_BCLK, GPIO_I2S_LRCK, GPIO_I2S_DATA);       // BCLK, DATA, LRCLK
  out->SetRate(SAMPLE_RATE);

  // ディスプレイの設定
  M5.Lcd.init();
  M5.Lcd.setRotation(3);
  M5.Lcd.setColorDepth(16);
  M5.Lcd.fillScreen(TFT_WHITE);

  // ToFセンサーの設定
  tof.setBus(&Wire);
  tof.setTimeout(500);
  tofok = tof.init();
  if (tofok) {
    tof.setDistanceMode(VL53L1X::Long);
    tof.setMeasurementTimingBudget(50000);
    tof.startContinuous(50);
  } else {
    sp("Failed to detect and initialize ToF sensor!");
    M5.Lcd.fillScreen(TFT_RED);
    delay(3000);
  }

  // アバターの設定
  avatar.usePSRAM(false);   // PSRAMを使わない（128x128ならメインメモリに収まるし速度も速いから）
  String tableNames[]   = { "body", "rhand", "lhand", "eyebrow", "eye", "mouth", "ahiru" };  // 部位名（imgTablesと順番を揃える）
  uint16_t* imgTables[] = { imgTableBody, imgTableRhand, imgTableLhand, imgTableEyebrow, imgTableEye, imgTableMouth, imgTableAhiru };  // 部位ごとにリスト化した表（テーブル）
  avatar.setImageData(imgInfo, tableNames, imgTables, 7); // 画像データを登録する
  avatar.useAntiAliases = true; // アンチエイリアス処理をする
  avatar.mirrorImage = false;   // 左右反転しない（反転はバグあり）
  avatar.setDrawDisplay(&M5.Lcd, 0,0, TFT_WHITE); // アバターの表示先を設定する（出力先, x, y, 背景色）
  avatar.debugtable();
  debug_free_memory("after avater setting");

  // アバターの表示
  defaultAvatarParts(); // アバターのパーツをデフォルトに設定する
  avatar.drawAvatar();  // アバター全体表示

  // ボタン押しながら電源を入れたらデバッグモードに移行
  if (M5.BtnA.isPressed()) {
    debugmode();  // ToFセンサー距離調整デバッグモード
  }

  // アバターのまばたきの設定と開始
  avatar.setBlink("eye", 1, 0);   // まばたき用のインデックス番号を設定する
  avatar.blink_wait1 = BLINK_TIME_BASE;  // 基本間隔
  avatar.blink_wait2 = BLINK_TIME_RAND;  // ランダム追加 max
  avatar.blink_wait3 = BLINK_TIME_CLOS;  // 閉じてる時間
  avatar.startAutoBlink();  // 自動まばたきスタート（タスク実行）
  blink = true;

  // アバターのリップシンクの設定と開始
  avatar.setLipsync("mouth", 0, 2, 3, 4, 5, 6);   // リップシンク用のインデックス番号を設定する
  avatar.startAutoLipsync();  // リップシンクをスタート（タスク実行）

  // ボイス再生
  playVoice(0, true);  // 「がんばるのだ」
  avatar.changeParts("mouth", 0);   // ボイス再生後は念のため口を戻す：口　閉じ　（むふ）
  avatar.drawAvatar();  // アバター全体表示

  // アバターに挿入するcanvasの設定
  tgauge.setColorDepth(16);
  tgauge.createSprite(20, 20);
  tgauge.fillSprite(0x0020);   // 透明色で塗りつぶし
  avatar.setInsertedLayer(0, OVR_FRONT_AVATER, &tgauge, 4,104);   // ゲージのcanvasをアバターに挿入設定
  if (SHOW_DISTANCE_GAUGE) {
    tinfo.setColorDepth(16);
    tinfo.createSprite(6, 128);
    tinfo.fillSprite(0x0020);   // 透明色で塗りつぶし
    avatar.setInsertedLayer(1, OVR_FRONT_AVATER, &tinfo, 121,0);   // ゲージのcanvasをアバターに挿入設定
  }

  // 準備完了
  sp("initialize done!");
  delay(1000);
}


// ====================================================================================

// メイン
void loop() {
  static uint32_t workingtime = 0;   // 働いた時間(s)
  static int stat = 0, laststat = 0;  // 現在の状態
  static int tired = 0;  // 疲労度 0～100%
  int distance = 0;   // 距離(mm)
  M5.update();

  // デバッグ用　ボタンを長押ししたら状態変更
  if (M5.BtnA.pressedFor(500)) {
    while (!M5.BtnA.wasReleased()) {  // ボタンを離すまで待機　★★★
      M5.update();
      delay(5);
    }
    M5.update();
    if (stat == 1) workingtime = WORK_LIMIT * ((float)TIRED_LIMIT_1 / 100);
    else if (stat == 2) workingtime = WORK_LIMIT * ((float)TIRED_LIMIT_2 / 100);
    else if (stat == 3) workingtime = WORK_LIMIT;
  }

  // ボタンを短押ししたら強制休憩モード
  if (M5.BtnA.wasReleased()) {
    sp("Lets break!");
    workingtime = WORK_LIMIT;
  }

  // 一定時間ごとに距離を計測する
  static uint32_t tm = 0;
  if (tm < millis() || laststat == 0) {
    bool hit = false;
    distance = (tofok) ? tof.readRangeContinuousMillimeters() : 0;
    if (distance >= DISPLAY_DISTANCE_MIN && distance <= DISPLAY_DISTANCE_MAX) {
      workingtime += 1;
      hit = true;
    }
    tm = millis() + 1000;  // 次回は1秒後に測定
    // 疲労レベルを計算する
    tired = ((float)workingtime / WORK_LIMIT) * 100;
    if (tired > 100) tired = 100;
    if (tired < TIRED_LIMIT_1) stat = 1;   // 元気
    else if (tired < TIRED_LIMIT_2) stat = 2;  // 少し疲れた
    else if (tired < 100) stat = 3; // かなり疲れた
    else stat = 4;  // もうムリ
    spf("distance=%d workingtime=%d tired=%d stat=%d\n", distance, workingtime, tired, stat);
    // 疲労ゲージと距離ゲージを作成する
    drawHpGage(&tgauge, tired);
    if (SHOW_DISTANCE_GAUGE) {
      drawDistanceGage(&tinfo, distance, DISPLAY_DISTANCE_MIN, DISPLAY_DISTANCE_MAX, 1500);
    }
    // アヒルちゃんのオンオフ
    if (SHOW_DISTANCE_AHIRU) {
      avatar.changeParts("ahiru", hit ? 1 : 0);   // アヒル
    }
    refresh = true;
  }

  // 状態stat変更時に1回のみ行う処理
  if (stat != laststat) {
    spp("new stat",stat);
    switch (stat) {
      case 1: // 元気
        defaultAvatarParts(); // アバターのパーツをデフォルトに戻す
        avatar.changeParts("eye", 1);     // 目　普通　開く（中）
        blinkOpen = 1;  // まばたき：目　普通　開く（中）
        blink = true;
        kyoroEyeSelect = 0;   // キョロキョロする目　普通
        kyoro = true;
        break;
      case 2: // 少し疲れた
        defaultAvatarParts(); // アバターのパーツをデフォルトに戻す
        avatar.changeParts("rhand", 1);   // 右腕　腰
        avatar.changeParts("lhand", 5);   // 左腕　考える
        avatar.changeParts("eyebrow", 2); // 眉　／ ＼　（下がり眉）
        avatar.changeParts("eye", 4);     // 目　普通2　開く（中）
        blinkOpen = 4;  // まばたき：目　普通2　開く（中）
        blink = true;
        kyoroEyeSelect = 1;   // キョロキョロする目　普通2
        kyoro = true;
        break;
      case 3: // かなり疲れた
        defaultAvatarParts(); // アバターのパーツをデフォルトに戻す
        avatar.changeParts("body", 1);    // 体　萎え
        avatar.changeParts("rhand", 3);   // 右腕　口元
        avatar.changeParts("lhand", 3);   // 左腕　口元
        avatar.changeParts("eyebrow", 4); // 眉　困り眉2
        avatar.changeParts("eye", 7);     // 目　普通3　開く（中）
        avatar.changeParts("mouth", 9);   // 口　んへー
        blinkOpen = 7;  // まばたき：目　普通3　開く（中）
        blink = true;
        kyoroEyeSelect = 2;   // キョロキョロする目　普通3
        kyoro = true;
        break;
      case 4: // もうムリ
        blinkOpen = 1;  // まばたき（しない）：目　普通　開く（中）
        blink = false;
        kyoro = false;
        break;
    }
    refresh = true;
  }

  // まばたき（blink変更時に1回のみ行う処理）
  static bool lastblink = false;
  if (blink != lastblink) {
    if (blink) {
      avatar.setBlink("eye", blinkOpen, 0);   // まばたき用のインデックス番号を設定する
      avatar.startAutoBlink();  // 自動まばたきスタート（タスク実行）
    } else {
      avatar.stopAutoBlink();  // 自動まばたき停止
      avatar.changeParts("eye", blinkOpen);     // 目を戻す
      refresh = true;
    }
    lastblink = blink;
  }

  // キョロキョロ（kyoro変更時に1回のみ行う処理）
  static bool lastkyoro = false;
  if (kyoro != lastkyoro) {
    if (kyoro) {
      startKyorokyoro();  // キョロキョロ開始
    } else {
      stopKyorokyoro();  // キョロキョロ終了
      avatar.changeParts("eye", blinkOpen);     // 目を戻す
      refresh = true;
    }
    lastkyoro = kyoro;
  }

  // 一旦ここでアバターを表示する
  if (refresh) {
    avatar.drawAvatar(); // アバター全体表示
    refresh = false;
  }
  laststat = stat;
  delay(5);

  // 状態別の動作 1～3  状態：1=元気 2=少し疲れた、3=かなり疲れた
  if (stat >= 1 && stat <= 3) return;    // まだ頑張れるときはここで抜ける

  // 状態別の動作 4
  bool interrupt;
  if (stat == 4) {    // 状態：4=もうムリ
    // 体力ゲージと距離ゲージを一時的に非表示にする
    avatar.changeInsertedLayerPosition(0, OVR_NONE);   // 体力ゲージ
    if (SHOW_DISTANCE_GAUGE) avatar.changeInsertedLayerPosition(1, OVR_NONE);   // 距離ゲージ

    // 表情変更
    avatar.changeParts("body", 0);    // 体
    avatar.changeParts("rhand", 2);   // 右腕　上げ
    avatar.changeParts("lhand", 2);   // 左腕　上げ
    avatar.changeParts("eyebrow", 1); // 眉　＼ ／　（怒り眉）
    avatar.changeParts("eye", 1);     // 目　普通　開く（中）
    avatar.changeParts("mouth", 0);   // 口　閉じ　（むふ）
    avatar.changeParts("ahiru", 0);   // アヒル
    avatar.drawAvatar();  // アバター全体表示

    // ボイス再生
    playVoice(1, true);  // 「ちょっと休憩するのだ」★★★
    avatar.changeParts("mouth", 0);   // ボイス再生後は念のため口を戻す：口　閉じ　（むふ）
    avatar.drawAvatar();  // アバター全体表示
    interrupt = waitMillis(500);  // 待機（このループ内で待機する）★★★

    // 自動まばたきの再開
    if (!blink) {
      avatar.setBlink("eye", blinkOpen, 0);   // まばたき用のインデックス番号を設定する
      avatar.startAutoBlink();  // 自動まばたきスタート（タスク実行）
      blink = true;
      lastblink = blink;
    }

    // 休憩開始
    uint32_t tm2 = millis() + BREAK_TIME_LONG * 1000;
    uint32_t tmb = millis() + 500;
    uint16_t kp = 0;
    while (millis() < tm2) {
      // ぼよんぼよんする
      float scx[4] = { 1.0, 0.99, 0.98, 0.99 };
      float scy[4] = { 1.0, 1.01, 1.02, 1.01 };
      for (int i=0; i<4; i++) {
        interrupt = waitMillis(75);  // 待機（このループ内で待機する）★★★
        if (interrupt) break;
        avatar.scaleBodyCanvasY = scx[i];
        avatar.scaleBodyCanvasX = scy[i];
        // 手を上げ下げする
        if (tmb < millis()) {
          tmb = millis() + 500;
          if (kp % 2 == 0) {
            avatar.changeParts("rhand", 1);   // 右腕　腰
            avatar.changeParts("lhand", 1);   // 左腕　腰
            avatar.changeParts("mouth", 0);   // 口　閉じ　（むふ）
          } else {
            avatar.changeParts("rhand", 2);   // 右腕　上げ
            avatar.changeParts("lhand", 2);   // 左腕　上げ
            avatar.changeParts("mouth", 1);   // 口　普通　（ほあー）
          }
          kp++;
        }
        avatar.drawAvatar();
      } //for
      if (interrupt) break;
    } //while

    // 休憩終了
    avatar.scaleBodyCanvasY = 1.0;
    avatar.scaleBodyCanvasX = 1.0;
    defaultAvatarParts(); // アバターのパーツをデフォルトに戻す
    avatar.drawAvatar(); // アバター全体表示

    // ボイス再生
    playVoice(2, true);  // 「そろそろいいのだ」★★★
    avatar.changeParts("mouth", 0);   // ボイス再生後は念のため口を戻す：口　閉じ　（むふ）
    avatar.drawAvatar();  // アバター全体表示

    // ボタン押下判定のバッファクリア
    if (interrupt) {
      M5.update();
      M5.BtnA.wasReleased();
    }

    // 体力ゲージと距離ゲージを再表示する
    avatar.changeInsertedLayerPosition(0, OVR_FRONT_AVATER);   // 体力ゲージ
    if (SHOW_DISTANCE_GAUGE) avatar.changeInsertedLayerPosition(1, OVR_FRONT_AVATER);   // 距離ゲージ
    workingtime = 0;
    laststat = stat;
    stat = 1;
  } // if stat==4
}
