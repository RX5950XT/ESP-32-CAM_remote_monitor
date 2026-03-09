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

static void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.printf("Connecting to WiFi: %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
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

    // Initialize camera
    if (!cameraInit()) {
        Serial.println("FATAL: Camera init failed, restarting...");
        delay(3000);
        ESP.restart();
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

    sendMessageTelegram("🟢 ESP32-CAM 監控系統已啟動！\n輸入 /start 查看可用指令。");

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

    // Periodic monitoring: capture and send photo
    if (shouldRunMonitor(schedulerState)) {
        Serial.println("Scheduled monitor capture...");
        camera_fb_t* fb = capturePhoto(FRAMESIZE_SVGA);
        if (fb) {
            sendPhotoTelegram(fb, "定期監控");
            esp_camera_fb_return(fb);
        } else {
            sendMessageTelegram("❌ 定期拍照失敗");
        }
    }

    // AI detection: capture, analyze, alert if person detected
    if (shouldRunAI(schedulerState)) {
        Serial.println("Scheduled AI detection...");
        camera_fb_t* fb = capturePhoto(FRAMESIZE_VGA);
        if (fb) {
            String aiResponse;
            bool personDetected = analyzePhotoForHuman(fb, aiResponse);
            if (personDetected) {
                Serial.println("ALERT: Person detected!");
                delay(1000);
                sendPhotoTelegram(fb, "🚨 AI 偵測到有人！");
            } else {
                Serial.println("AI: No person detected");
            }
            esp_camera_fb_return(fb);
        } else {
            sendMessageTelegram("❌ AI 偵測拍照失敗");
        }
    }

    delay(10);
}
