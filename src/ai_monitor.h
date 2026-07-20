#pragma once

#include <Arduino.h>
#include "esp_camera.h"

// Initialize on-device TFLite person detection model (call once in setup).
// Returns true on success.
bool personDetectInit();

// Capture 96x96 grayscale, run local person_detection model.
// Returns true if a person is detected (score thresholds only; cooldown is caller's job).
// debugInfo receives score summary for Serial / optional Telegram use.
bool detectPersonLocal(String& debugInfo);

// Backward-compatible name used by older call sites.
// Ignores fb (local model captures its own grayscale frame).
bool analyzePhotoForHuman(camera_fb_t* fb, String& aiResponse);
