#pragma once

#include "esp_camera.h"
#include <stdint.h>

// AI Thinker ESP32-CAM pin definitions
#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM   0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM     5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22

// Default: JPEG mode for Telegram photos.
bool cameraInit();

struct CameraSettings {
    int brightness = 0;
    int contrast = 0;
    int saturation = 0;
    int exposureLevel = 1;
    int gainCeiling = GAINCEILING_16X;
    int jpegQuality = 8;
};

CameraSettings cameraSettings();
// Automatic exposure, gain and white balance remain enabled in every mode.
bool cameraSetSettings(const CameraSettings& settings);

// Capture JPEG at the requested frame size (camera must be in JPEG mode).
camera_fb_t* capturePhoto(framesize_t frameSize);

// Capture one 96x96 grayscale frame into outBuf (must be kMaxImageSize bytes).
// Switches to grayscale only if needed; leaves gray mode until JPEG capture needs it
// (lazy restore) to avoid per-frame deinit/init thrashing.
// Returns true on success.
bool captureGray96(uint8_t* outBuf, size_t outBufLen);
