#include "device_control.h"
#include "ai_monitor.h"
#include "telegram.h"
#include "device_identity.h"
#include <Preferences.h>
#include <WiFi.h>
#include <math.h>
#include <string.h>

#ifndef PERSON_MIN_SCORE
#define PERSON_MIN_SCORE 0.70f
#endif
#ifndef PERSON_SCORE_MARGIN
#define PERSON_SCORE_MARGIN 0.20f
#endif
#ifndef ALERT_COOLDOWN_SEC
#define ALERT_COOLDOWN_SEC 0
#endif

namespace {
constexpr size_t kSettingsBytes = 1024;
constexpr unsigned long kActionIntervalMs = 3000;
constexpr unsigned long kAlertRetryMs = 30000;
struct Settings {
    CameraSettings camera;
    bool monitorEnabled = false;
    bool aiEnabled = false;
    int monitorInterval = DEFAULT_MONITOR_INTERVAL_SEC;
    int aiInterval = DEFAULT_AI_INTERVAL_SEC;
    float minimum = PERSON_MIN_SCORE;
    float margin = PERSON_SCORE_MARGIN;
    int cooldown = ALERT_COOLDOWN_SEC;
};
struct Health {
    bool available = false;
    bool success = false;
    bool hadSuccess = false;
    unsigned long attempted = 0;
    unsigned long succeeded = 0;
    String error;
    bool hadError = false;
    unsigned long failed = 0;
    String lastError;
};
Preferences preferences;
bool storageOpen = false;
bool settingsPersisted = false;
String settingsError;
Settings active;
Health cameraHealth, telegramHealth, inferenceHealth, mcpPhotoHealth, alertHealth;
PersonDetectionResult latest;
bool detectionAvailable = false;
unsigned long detectionCompleted = 0;
unsigned long lastAction = 0;
bool actionValid = false;
unsigned long lastAlertSuccess = 0;
bool alertSuccessValid = false;
unsigned long lastAlertFailure = 0;
bool alertFailureValid = false;

void record(Health& health, bool success, const String& error) {
    health.available = true;
    health.success = success;
    health.attempted = millis();
    health.error = success ? String() : error;
    if (success) {
        health.hadSuccess = true;
        health.succeeded = health.attempted;
    } else {
        health.hadError = true;
        health.failed = health.attempted;
        health.lastError = error;
    }
}

void healthJson(JsonObject data, const Health& health) {
    data["available"] = health.available;
    data["success"] = nullptr;
    data["last_attempt_uptime_sec"] = nullptr;
    data["last_success_uptime_sec"] = nullptr;
    data["error"] = nullptr;
    data["last_error"] = nullptr;
    data["last_error_uptime_sec"] = nullptr;
    if (health.available) {
        data["success"] = health.success;
        data["last_attempt_uptime_sec"] = health.attempted / 1000UL;
        if (!health.success) data["error"] = health.error;
    }
    if (health.hadSuccess) data["last_success_uptime_sec"] = health.succeeded / 1000UL;
    if (health.hadError) {
        data["last_error"] = health.lastError;
        data["last_error_uptime_sec"] = health.failed / 1000UL;
    }
}

bool integer(JsonVariantConst value, int minimum, int maximum, int& output) {
    if (value.is<bool>() || !value.is<int>()) return false;
    const int number = value.as<int>();
    if (number < minimum || number > maximum) return false;
    output = number;
    return true;
}

bool score(JsonVariantConst value, float& output) {
    if (value.is<bool>() || !value.is<float>()) return false;
    const float number = value.as<float>();
    if (!isfinite(number) || number < 0 || number > 1) return false;
    output = number;
    return true;
}

bool cameraPatch(JsonObjectConst args, Settings& value) {
    for (JsonPairConst field : args) {
        const char* key = field.key().c_str();
        int* target = nullptr;
        int minimum = -2, maximum = 2;
        if (!strcmp(key, "brightness")) target = &value.camera.brightness;
        else if (!strcmp(key, "contrast")) target = &value.camera.contrast;
        else if (!strcmp(key, "saturation")) target = &value.camera.saturation;
        else if (!strcmp(key, "exposure_level")) target = &value.camera.exposureLevel;
        else if (!strcmp(key, "gain_ceiling")) {
            target = &value.camera.gainCeiling;
            minimum = 0; maximum = 4;
        } else if (!strcmp(key, "jpeg_quality")) {
            target = &value.camera.jpegQuality;
            minimum = 8; maximum = 20;
        }
        if (!target || !integer(field.value(), minimum, maximum, *target)) return false;
    }
    return true;
}

bool monitorPatch(JsonObjectConst args, Settings& value) {
    for (JsonPairConst field : args) {
        const char* key = field.key().c_str();
        if (!strcmp(key, "monitor_enabled") || !strcmp(key, "ai_enabled")) {
            if (!field.value().is<bool>()) return false;
            if (!strcmp(key, "monitor_enabled")) value.monitorEnabled = field.value().as<bool>();
            else value.aiEnabled = field.value().as<bool>();
        } else if (!strcmp(key, "monitor_interval_sec")) {
            if (!integer(field.value(), MIN_MONITOR_INTERVAL_SEC, MAX_MONITOR_INTERVAL_SEC,
                         value.monitorInterval)) return false;
        } else if (!strcmp(key, "ai_interval_sec")) {
            if (!integer(field.value(), MIN_AI_INTERVAL_SEC, MAX_AI_INTERVAL_SEC,
                         value.aiInterval)) return false;
        } else return false;
    }
    return true;
}

bool detectionPatch(JsonObjectConst args, Settings& value) {
    for (JsonPairConst field : args) {
        const char* key = field.key().c_str();
        if (!strcmp(key, "person_min_score")) {
            if (!score(field.value(), value.minimum)) return false;
        } else if (!strcmp(key, "person_score_margin")) {
            if (!score(field.value(), value.margin)) return false;
        } else if (!strcmp(key, "alert_cooldown_sec")) {
            if (!integer(field.value(), 0, 86400, value.cooldown)) return false;
        } else return false;
    }
    return true;
}

bool patch(const char* group, JsonObjectConst args, Settings& value) {
    if (!group || args.isNull() || args.size() == 0) return false;
    if (!strcmp(group, "camera")) return cameraPatch(args, value);
    if (!strcmp(group, "monitor")) return monitorPatch(args, value);
    if (!strcmp(group, "detection")) return detectionPatch(args, value);
    return false;
}

void settingsJson(JsonObject data, const Settings& value) {
    data["version"] = 1;
    JsonObject camera = data["camera"].to<JsonObject>();
    camera["brightness"] = value.camera.brightness;
    camera["contrast"] = value.camera.contrast;
    camera["saturation"] = value.camera.saturation;
    camera["exposure_level"] = value.camera.exposureLevel;
    camera["gain_ceiling"] = value.camera.gainCeiling;
    camera["jpeg_quality"] = value.camera.jpegQuality;
    JsonObject monitor = data["monitor"].to<JsonObject>();
    monitor["monitor_enabled"] = value.monitorEnabled;
    monitor["ai_enabled"] = value.aiEnabled;
    monitor["monitor_interval_sec"] = value.monitorInterval;
    monitor["ai_interval_sec"] = value.aiInterval;
    JsonObject detection = data["detection"].to<JsonObject>();
    detection["person_min_score"] = value.minimum;
    detection["person_score_margin"] = value.margin;
    detection["alert_cooldown_sec"] = value.cooldown;
}

bool cameraEqual(const CameraSettings& a, const CameraSettings& b) {
    return a.brightness == b.brightness && a.contrast == b.contrast &&
        a.saturation == b.saturation && a.exposureLevel == b.exposureLevel &&
        a.gainCeiling == b.gainCeiling && a.jpegQuality == b.jpegQuality;
}

bool equal(const Settings& a, const Settings& b) {
    return cameraEqual(a.camera, b.camera) && a.monitorEnabled == b.monitorEnabled &&
        a.aiEnabled == b.aiEnabled && a.monitorInterval == b.monitorInterval &&
        a.aiInterval == b.aiInterval && a.minimum == b.minimum &&
        a.margin == b.margin && a.cooldown == b.cooldown;
}

Settings current(const SchedulerState& state) {
    Settings value = active;
    value.camera = cameraSettings();
    value.monitorEnabled = state.monitorEnabled;
    value.aiEnabled = state.aiEnabled;
    value.monitorInterval = state.monitorIntervalSec;
    value.aiInterval = state.aiIntervalSec;
    return value;
}

void applyScheduler(const Settings& value, SchedulerState& state) {
    if (state.monitorEnabled != value.monitorEnabled) state.lastMonitorTime = millis();
    if (state.aiEnabled != value.aiEnabled) state.lastAITime = millis();
    state.monitorEnabled = value.monitorEnabled;
    state.aiEnabled = value.aiEnabled;
    state.monitorIntervalSec = value.monitorInterval;
    state.aiIntervalSec = value.aiInterval;
    active = value;
}

bool openStorage() {
    if (!storageOpen) storageOpen = preferences.begin("remote-monitor", false);
    if (!storageOpen) settingsError = "設定儲存區無法開啟";
    return storageOpen;
}

bool save(const Settings& value) {
    if (!openStorage()) return false;
    JsonDocument document;
    settingsJson(document.to<JsonObject>(), value);
    char bytes[kSettingsBytes];
    const size_t length = measureJson(document);
    if (document.overflowed() || length >= sizeof(bytes)) {
        settingsError = "設定資料太大，無法保存";
        return false;
    }
    serializeJson(document, bytes, sizeof(bytes));
    // NVS commits one versioned blob, so settings cannot be partially updated.
    if (preferences.putBytes("settings", bytes, length) != length) {
        settingsError = "設定保存失敗，已保留原設定";
        return false;
    }
    settingsPersisted = true;
    settingsError = "";
    return true;
}

unsigned long remaining(unsigned long since, unsigned long duration, bool valid) {
    if (!valid || millis() - since >= duration) return 0;
    return (duration - (millis() - since) + 999UL) / 1000UL;
}
}  // namespace

bool deviceControlInit(SchedulerState& state) {
    if (storageOpen) preferences.end();
    storageOpen = false;
    settingsPersisted = false;
    settingsError = "";
    active = Settings();
    const CameraSettings previousCamera = cameraSettings();
    state = SchedulerState();
    cameraHealth = telegramHealth = inferenceHealth = mcpPhotoHealth = alertHealth = Health();
    latest = PersonDetectionResult();
    detectionAvailable = false;
    detectionCompleted = 0;
    actionValid = alertSuccessValid = alertFailureValid = false;
    if (!cameraEqual(previousCamera, active.camera) && !cameraSetSettings(active.camera)) {
        settingsError = "相機預設設定無法套用";
        record(cameraHealth, false, settingsError);
        return false;
    }
    if (!openStorage()) return false;
    if (!preferences.isKey("settings")) return true;
    const size_t length = preferences.getBytesLength("settings");
    char bytes[kSettingsBytes];
    JsonDocument document;
    if (length == 0 || length >= sizeof(bytes) ||
        preferences.getBytes("settings", bytes, length) != length ||
        deserializeJson(document, bytes, length)) {
        settingsError = "保存設定已損壞，目前使用預設值";
        return false;
    }
    Settings loaded;
    JsonObjectConst root = document.as<JsonObjectConst>();
    JsonObjectConst camera = root["camera"].as<JsonObjectConst>();
    JsonObjectConst monitor = root["monitor"].as<JsonObjectConst>();
    JsonObjectConst detection = root["detection"].as<JsonObjectConst>();
    if (root.size() != 4 || root["version"].is<bool>() || !root["version"].is<int>() ||
        root["version"].as<int>() != 1 || camera.size() != 6 || monitor.size() != 4 ||
        detection.size() != 3 || !patch("camera", camera, loaded) ||
        !patch("monitor", monitor, loaded) || !patch("detection", detection, loaded)) {
        settingsError = "保存設定格式無效，目前使用預設值";
        return false;
    }
    if (!cameraEqual(loaded.camera, active.camera) && !cameraSetSettings(loaded.camera)) {
        cameraSetSettings(active.camera);
        settingsError = "保存的相機設定無法套用";
        return false;
    }
    applyScheduler(loaded, state);
    settingsPersisted = true;
    return true;
}

bool deviceConfigure(const char* group, JsonObjectConst args, SchedulerState& state, String& error) {
    error = "";
    const Settings before = current(state);
    Settings next = before;
    if (!patch(group, args, next)) {
        error = "參數名稱、型別或範圍無效，且至少要提供一項設定";
        return false;
    }
    if (equal(before, next) && settingsPersisted && !settingsError.length()) return true;
    const bool cameraChanged = !cameraEqual(before.camera, next.camera);
    if (cameraChanged && !cameraSetSettings(next.camera)) {
        const bool restored = cameraSetSettings(before.camera);
        error = restored ? "相機設定失敗，已保留原設定" : "相機設定及還原失敗";
        record(cameraHealth, false, error);
        return false;
    }
    if (!save(next)) {
        error = settingsError;
        if (cameraChanged && !cameraSetSettings(before.camera)) {
            error += "；相機還原失敗";
            record(cameraHealth, false, error);
        }
        return false;
    }
    applyScheduler(next, state);
    return true;
}

void deviceDetectionStatus(JsonObject data) {
    data["device_id"] = deviceId();
    data["device_name"] = deviceDisplayName();
    data["available"] = detectionAvailable;
    data["success"] = nullptr;
    data["person_detected"] = nullptr;
    data["person_score"] = nullptr;
    data["no_person_score"] = nullptr;
    data["duration_ms"] = nullptr;
    data["completed_uptime_sec"] = nullptr;
    data["error"] = nullptr;
    if (!detectionAvailable) return;
    data["success"] = latest.success;
    data["duration_ms"] = latest.durationMs;
    data["completed_uptime_sec"] = detectionCompleted / 1000UL;
    if (!latest.success) {
        data["error"] = latest.error;
        return;
    }
    data["person_detected"] = latest.personDetected;
    data["person_score"] = latest.personScore;
    data["no_person_score"] = latest.noPersonScore;
}

void deviceStatus(JsonObject data, const SchedulerState& state) {
    deviceIdentityStatus(data);
    const CameraSettings value = cameraSettings();
    data["automatic_exposure"] = true;
    data["automatic_gain"] = true;
    data["automatic_white_balance"] = true;
    data["brightness"] = value.brightness;
    data["contrast"] = value.contrast;
    data["saturation"] = value.saturation;
    data["exposure_level"] = value.exposureLevel;
    data["gain_ceiling"] = value.gainCeiling;
    data["jpeg_quality"] = value.jpegQuality;
    data["monitor_enabled"] = state.monitorEnabled;
    data["ai_enabled"] = state.aiEnabled;
    data["monitor_interval_sec"] = state.monitorIntervalSec;
    data["ai_interval_sec"] = state.aiIntervalSec;
    data["person_min_score"] = active.minimum;
    data["person_score_margin"] = active.margin;
    data["alert_cooldown_sec"] = active.cooldown;
    data["alert_retry_cooldown_sec"] = kAlertRetryMs / 1000UL;
    data["alert_cooldown_remaining_sec"] = remaining(lastAlertSuccess,
        static_cast<unsigned long>(active.cooldown) * 1000UL, alertSuccessValid);
    data["alert_retry_remaining_sec"] = remaining(lastAlertFailure, kAlertRetryMs, alertFailureValid);
    data["settings_persisted"] = settingsPersisted;
    data["settings_error"] = nullptr;
    if (settingsError.length()) data["settings_error"] = settingsError;
    data["wifi_rssi_dbm"] = WiFi.RSSI();
    data["psram"] = psramFound();
    data["free_heap_bytes"] = ESP.getFreeHeap();
    data["free_psram_bytes"] = ESP.getFreePsram();
    data["uptime_sec"] = millis() / 1000UL;
    deviceDetectionStatus(data["detection"].to<JsonObject>());
    JsonObject health = data["health"].to<JsonObject>();
    healthJson(health["camera"].to<JsonObject>(), cameraHealth);
    healthJson(health["telegram"].to<JsonObject>(), telegramHealth);
    healthJson(health["inference"].to<JsonObject>(), inferenceHealth);
    healthJson(health["mcp_photo"].to<JsonObject>(), mcpPhotoHealth);
    healthJson(health["alert"].to<JsonObject>(), alertHealth);
}

bool deviceDetect(String& error) {
    latest = runPersonDetection(active.minimum, active.margin);
    detectionAvailable = true;
    detectionCompleted = millis();
    error = latest.error;
    if (!latest.success && error.length() == 0) error = latest.error = "人物偵測失敗";
    record(inferenceHealth, latest.success, error);
    return latest.success;
}

camera_fb_t* deviceCapture(framesize_t resolution) {
    camera_fb_t* frame = capturePhoto(resolution);
    record(cameraHealth, frame != nullptr, "拍照失敗");
    return frame;
}

bool deviceSendPhoto(framesize_t resolution, const char* caption, String& error) {
    error = "";
    camera_fb_t* frame = deviceCapture(resolution);
    if (!frame) {
        error = "拍照失敗";
        return false;
    }
    const bool sent = sendPhotoTelegram(frame, caption);
    esp_camera_fb_return(frame);
    if (!sent) error = "Telegram 照片傳送失敗";
    return sent;
}

void deviceRecordTelegram(bool success) {
    record(telegramHealth, success, "Telegram 傳送失敗");
}

void deviceRecordMcpPhoto(bool success) {
    record(mcpPhotoHealth, success, "MCP 照片傳送中斷");
}

bool deviceResolution(const char* name, framesize_t& resolution) {
    if (!name || !strcmp(name, "svga")) resolution = FRAMESIZE_SVGA;
    else if (!strcmp(name, "vga")) resolution = FRAMESIZE_VGA;
    else if (!strcmp(name, "xga")) resolution = FRAMESIZE_XGA;
    else if (!strcmp(name, "uxga")) resolution = FRAMESIZE_UXGA;
    else return false;
    return true;
}

bool deviceActionReady(String& error) {
    error = "";
    if (!remaining(lastAction, kActionIntervalMs, actionValid)) return true;
    error = "Busy: 請在上次操作完成 3 秒後重試";
    return false;
}

void deviceActionDone() {
    lastAction = millis();
    actionValid = true;
}

void deviceRunScheduledAI() {
    String error;
    if (!deviceDetect(error)) {
        Serial.printf("Local AI failed: %s\n", error.c_str());
        return;
    }
    if (!latest.personDetected) {
        Serial.println("Local AI: No person detected");
        return;
    }
    if (remaining(lastAlertSuccess, static_cast<unsigned long>(active.cooldown) * 1000UL,
                  alertSuccessValid) || remaining(lastAlertFailure, kAlertRetryMs, alertFailureValid)) {
        Serial.println("Person detected; alert cooldown active");
        return;
    }
    const bool sent = deviceSendPhoto(FRAMESIZE_VGA, latest.summary.c_str(), error);
    record(alertHealth, sent, error);
    if (sent) {
        lastAlertSuccess = millis();
        alertSuccessValid = true;
        alertFailureValid = false;
    } else {
        lastAlertFailure = millis();
        alertFailureValid = true;
        Serial.printf("Person alert failed: %s\n", error.c_str());
        sendMessageTelegram("🚨 偵測到有人，但警報照片傳送失敗");
    }
}
