#pragma once

// ================================================================
// Select your Camera Board Model by uncommenting ONE of the lines:
// ================================================================
#define CAMERA_MODEL_ESP32S3_WROOM_CAM    // Freenove / Generic ESP32-S3-WROOM CAM (Most Common)
// #define CAMERA_MODEL_XIAO_ESP32S3         // Seeed XIAO ESP32S3 Sense
// #define CAMERA_MODEL_WAVESHARE_ESP32S3    // Waveshare ESP32-S3-CAM
// #define CAMERA_MODEL_AI_THINKER           // Classic ESP32-CAM (AI Thinker OV2640)

// ----------------------------------------------------------------
// 1. Generic ESP32-S3-WROOM CAM / Freenove ESP32-S3-CAM
// ----------------------------------------------------------------
#if defined(CAMERA_MODEL_ESP32S3_WROOM_CAM)
  #define PWDN_GPIO_NUM     -1
  #define RESET_GPIO_NUM    -1
  #define XCLK_GPIO_NUM     15
  #define SIOD_GPIO_NUM      4
  #define SIOC_GPIO_NUM      5

  #define Y9_GPIO_NUM       16
  #define Y8_GPIO_NUM       17
  #define Y7_GPIO_NUM       18
  #define Y6_GPIO_NUM       12
  #define Y5_GPIO_NUM       10
  #define Y4_GPIO_NUM        8
  #define Y3_GPIO_NUM        9
  #define Y2_GPIO_NUM       11
  #define VSYNC_GPIO_NUM     6
  #define HREF_GPIO_NUM      7
  #define PCLK_GPIO_NUM     13

// ----------------------------------------------------------------
// 2. Seeed Studio XIAO ESP32S3 Sense
// ----------------------------------------------------------------
#elif defined(CAMERA_MODEL_XIAO_ESP32S3)
  #define PWDN_GPIO_NUM     -1
  #define RESET_GPIO_NUM    -1
  #define XCLK_GPIO_NUM     10
  #define SIOD_GPIO_NUM     40
  #define SIOC_GPIO_NUM     39

  #define Y9_GPIO_NUM       48
  #define Y8_GPIO_NUM       11
  #define Y7_GPIO_NUM       12
  #define Y6_GPIO_NUM       14
  #define Y5_GPIO_NUM       16
  #define Y4_GPIO_NUM       18
  #define Y3_GPIO_NUM       17
  #define Y2_GPIO_NUM       15
  #define VSYNC_GPIO_NUM    38
  #define HREF_GPIO_NUM     47
  #define PCLK_GPIO_NUM     13

// ----------------------------------------------------------------
// 3. Waveshare ESP32-S3-CAM
// ----------------------------------------------------------------
#elif defined(CAMERA_MODEL_WAVESHARE_ESP32S3)
  #define PWDN_GPIO_NUM     -1
  #define RESET_GPIO_NUM    -1
  #define XCLK_GPIO_NUM     40
  #define SIOD_GPIO_NUM     39
  #define SIOC_GPIO_NUM     38

  #define Y9_GPIO_NUM       13
  #define Y8_GPIO_NUM       47
  #define Y7_GPIO_NUM       14
  #define Y6_GPIO_NUM        3
  #define Y5_GPIO_NUM       42
  #define Y4_GPIO_NUM       46
  #define Y3_GPIO_NUM       45
  #define Y2_GPIO_NUM       41
  #define VSYNC_GPIO_NUM    21
  #define HREF_GPIO_NUM     18
  #define PCLK_GPIO_NUM     11

// ----------------------------------------------------------------
// 4. Classic AI Thinker ESP32-CAM (Classic ESP32, not S3)
// ----------------------------------------------------------------
#elif defined(CAMERA_MODEL_AI_THINKER)
  #define PWDN_GPIO_NUM     32
  #define RESET_GPIO_NUM    -1
  #define XCLK_GPIO_NUM      0
  #define SIOD_GPIO_NUM     26
  #define SIOC_GPIO_NUM     27

  #define Y9_GPIO_NUM       35
  #define Y8_GPIO_NUM       34
  #define Y7_GPIO_NUM       39
  #define Y6_GPIO_NUM       36
  #define Y5_GPIO_NUM       21
  #define Y4_GPIO_NUM       19
  #define Y3_GPIO_NUM       18
  #define Y2_GPIO_NUM        5
  #define VSYNC_GPIO_NUM    25
  #define HREF_GPIO_NUM     23
  #define PCLK_GPIO_NUM     22

#else
  #error "No camera model selected! Please uncomment your board model above."
#endif
