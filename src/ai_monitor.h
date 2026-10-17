#pragma once

#include <Arduino.h>
#include "esp_camera.h"

// Initialize on-device TFLite person detection model (call once in setup).
// Returns true on success.
bool personDetectInit();

struct PersonDetectionResult {
    bool success = false;
    bool personDetected = false;
    float personScore = 0.0f;
    float noPersonScore = 0.0f;
    unsigned long durationMs = 0;
    String error;
    String summary;
};

// A failed capture/inference is distinct from a successful "no person" result.
PersonDetectionResult runPersonDetection(float minimum, float margin);

// Capture 96x96 grayscale, run local person_detection model.
// Returns true if a person is detected (score thresholds only; cooldown is caller's job).
// debugInfo receives score summary for Serial / optional Telegram use.
bool detectPersonLocal(String& debugInfo);

// Backward-compatible name used by older call sites.
// Ignores fb (local model captures its own grayscale frame).
bool analyzePhotoForHuman(camera_fb_t* fb, String& aiResponse);
