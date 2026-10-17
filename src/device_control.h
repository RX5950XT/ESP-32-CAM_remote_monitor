#pragma once

#include <ArduinoJson.h>
#include "camera.h"
#include "scheduler.h"

// One control path for Telegram, MCP and scheduled work.
bool deviceControlInit(SchedulerState& state);
bool deviceConfigure(const char* group, JsonObjectConst args, SchedulerState& state, String& error);
void deviceStatus(JsonObject data, const SchedulerState& state);
void deviceDetectionStatus(JsonObject data);
bool deviceDetect(String& error);
camera_fb_t* deviceCapture(framesize_t resolution);
bool deviceSendPhoto(framesize_t resolution, const char* caption, String& error);
void deviceRecordTelegram(bool success);
void deviceRecordMcpPhoto(bool success);
bool deviceResolution(const char* name, framesize_t& resolution);
bool deviceActionReady(String& error);
void deviceActionDone();
void deviceRunScheduledAI();
