#pragma once

#include "esp_camera.h"

// Analyze a photo for human presence using OpenRouter API.
// Returns true if a person is detected.
// The caller is responsible for returning the frame buffer.
bool analyzePhotoForHuman(camera_fb_t* fb);
