#pragma once

#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include "esp_camera.h"
#include "config.h"

// Forward declarations
struct SchedulerState;

void telegramInit(WiFiClientSecure& client);
bool sendPhotoTelegram(camera_fb_t* fb, const char* caption = nullptr);
void sendMessageTelegram(const String& message);
void handleTelegramMessages(SchedulerState& state);
