#pragma once

#include <Arduino.h>
#include "esp_camera.h"

// Analyze a photo for human presence using OpenRouter API.
// Returns true if a person is detected.
// aiResponse will contain the raw AI text response for debugging.
// The caller is responsible for returning the frame buffer.
bool analyzePhotoForHuman(camera_fb_t* fb, String& aiResponse);
