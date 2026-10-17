#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "camera.h"
#include "telegram.h"
#include "scheduler.h"
#include "ai_monitor.h"
#include "mcp_server.h"
#include "device_control.h"
#include "device_identity.h"

static WiFiClientSecure secureClient;
static SchedulerState schedulerState;

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

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== ESP32-CAM Remote Monitor ===");

    // Initialize camera (JPEG mode)
    if (!cameraInit()) {
        Serial.println("FATAL: Camera init failed, restarting...");
        delay(3000);
        ESP.restart();
    }

    if (!deviceControlInit(schedulerState)) {
        Serial.println("WARNING: Saved settings unavailable; inspect /status");
    }

    // Initialize local TFLite person detection
    if (!personDetectInit()) {
        Serial.println("WARNING: Local person detection init failed");
    }

    // Connect to WiFi
    connectWiFi();
    mcpInit();
    Serial.println(deviceTag());
    Serial.println(deviceConnectionMessage());

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
        String("🟢 ESP32-CAM 監控系統已啟動！\n") + deviceConnectionMessage() + "\n"
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

    if (handleMcpClient(schedulerState)) return;

    // Handle Telegram commands
    if (handleTelegramMessages(schedulerState)) return;

    // Only one heavy camera/network job per loop (AI preferred when both due).
    bool heavyJobRan = false;

    // Local person detection: gray inference, then JPEG alert if person found
    if (shouldRunAI(schedulerState)) {
        markAIRunning(schedulerState);
        heavyJobRan = true;
        Serial.println("Scheduled local person detection...");
        deviceRunScheduledAI();
        markAIDone(schedulerState);
    }

    // Periodic monitoring: capture and send photo
    if (!heavyJobRan && shouldRunMonitor(schedulerState)) {
        markMonitorRunning(schedulerState);
        Serial.println("Scheduled monitor capture...");
        String error;
        if (!deviceSendPhoto(FRAMESIZE_SVGA, "定期監控", error)) {
            Serial.printf("Scheduled monitor failed: %s\n", error.c_str());
            sendMessageTelegram("❌ 定期監控失敗：" + error);
        }
        markMonitorDone(schedulerState);
    }

    delay(10);
}
