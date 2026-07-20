#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "camera.h"
#include "telegram.h"
#include "scheduler.h"
#include "ai_monitor.h"

static WiFiClientSecure secureClient;
static SchedulerState schedulerState;
static unsigned long lastAlertMs = 0;
static bool lastAlertValid = false;

static void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.printf("Connecting to WiFi: %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi connection failed, will retry...");
    }
}

static bool alertCooldownActive() {
#if ALERT_COOLDOWN_SEC <= 0
    return false;
#else
    if (!lastAlertValid) return false;
    unsigned long elapsed = millis() - lastAlertMs;
    return elapsed < ((unsigned long)ALERT_COOLDOWN_SEC * 1000UL);
#endif
}

static void markAlertSent() {
#if ALERT_COOLDOWN_SEC > 0
    lastAlertMs = millis();
    lastAlertValid = true;
#endif
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== ESP32-CAM Remote Monitor ===");

    // Initialize camera (JPEG mode)
    if (!cameraInit()) {
        Serial.println("FATAL: Camera init failed, restarting...");
        delay(3000);
        ESP.restart();
    }

    // Initialize local TFLite person detection
    if (!personDetectInit()) {
        Serial.println("WARNING: Local person detection init failed");
    }

    // Connect to WiFi
    connectWiFi();

    // Set time via NTP (needed for HTTPS certificate validation)
    configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.print("Waiting for NTP time sync");
    time_t now = time(nullptr);
    int ntpAttempts = 0;
    while (now < 8 * 3600 * 2 && ntpAttempts < 20) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
        ntpAttempts++;
    }
    Serial.println();

    // Setup secure client (skip certificate verification for simplicity)
    secureClient.setInsecure();

    // Initialize Telegram bot
    telegramInit(secureClient);

    sendMessageTelegram(
        "🟢 ESP32-CAM 監控系統已啟動！\n"
        "人物偵測：本地 TFLite person_detection\n"
        "輸入 /start 查看可用指令。");

    Serial.println("Setup complete!");
}

void loop() {
    // Reconnect WiFi if disconnected
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected, reconnecting...");
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED) {
            delay(5000);
            return;
        }
    }

    // Handle Telegram commands
    handleTelegramMessages(schedulerState);

    // Only one heavy camera/network job per loop (AI preferred when both due).
    bool heavyJobRan = false;

    // Local person detection: gray inference, then JPEG alert if person found
    if (shouldRunAI(schedulerState)) {
        markAIRunning(schedulerState);
        heavyJobRan = true;
        Serial.println("Scheduled local person detection...");
        String debugInfo;
        bool personDetected = detectPersonLocal(debugInfo);
        if (personDetected) {
            if (alertCooldownActive()) {
                Serial.printf("Person detected but alert cooldown active (%ds)\n",
                              (int)ALERT_COOLDOWN_SEC);
            } else {
                Serial.println("ALERT: Person detected!");
                camera_fb_t* fb = capturePhoto(FRAMESIZE_VGA);
                if (fb) {
                    sendPhotoTelegram(fb, debugInfo.c_str());
                    esp_camera_fb_return(fb);
                    markAlertSent();
                } else {
                    sendMessageTelegram("🚨 偵測到有人，但警報拍照失敗\n" + debugInfo);
                    markAlertSent();  // still cool down to avoid spam on capture fail
                }
            }
        } else {
            Serial.println("Local AI: No person detected");
        }
        markAIDone(schedulerState);
    }

    // Periodic monitoring: capture and send photo
    if (!heavyJobRan && shouldRunMonitor(schedulerState)) {
        markMonitorRunning(schedulerState);
        Serial.println("Scheduled monitor capture...");
        camera_fb_t* fb = capturePhoto(FRAMESIZE_SVGA);
        if (fb) {
            sendPhotoTelegram(fb, "定期監控");
            esp_camera_fb_return(fb);
        } else {
            sendMessageTelegram("❌ 定期拍照失敗");
        }
        markMonitorDone(schedulerState);
    }

    delay(10);
}
