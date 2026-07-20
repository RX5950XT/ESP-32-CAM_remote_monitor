#include "telegram.h"
#include "camera.h"
#include "scheduler.h"
#include <ArduinoJson.h>

static UniversalTelegramBot* bot = nullptr;
static unsigned long lastBotCheck = 0;
static const unsigned long BOT_CHECK_INTERVAL = 1000;

void telegramInit(WiFiClientSecure& client) {
    bot = new UniversalTelegramBot(BOT_TOKEN, client);
    bot->longPoll = 0;
    Serial.println("Telegram bot initialized");
}

bool sendPhotoTelegram(camera_fb_t* fb, const char* caption) {
    if (!fb) return false;

    Serial.printf("Sending photo (%u bytes) to Telegram...\n", fb->len);

    WiFiClientSecure client;
    client.setInsecure();

    if (!client.connect("api.telegram.org", 443)) {
        Serial.println("Failed to connect to Telegram API");
        return false;
    }

    String boundary = "----ESP32CAMBoundary";
    String captionStr = caption ? caption : "";

    // Build multipart form-data parts
    String head = "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
        String(CHAT_ID) + "\r\n";

    if (captionStr.length() > 0) {
        head += "--" + boundary + "\r\n"
            "Content-Disposition: form-data; name=\"caption\"\r\n\r\n" +
            captionStr + "\r\n";
    }

    head += "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n";

    String tail = "\r\n--" + boundary + "--\r\n";

    size_t totalLen = head.length() + fb->len + tail.length();

    client.println("POST /bot" + String(BOT_TOKEN) + "/sendPhoto HTTP/1.1");
    client.println("Host: api.telegram.org");
    client.println("Content-Length: " + String(totalLen));
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.println("Connection: close");
    client.println();

    // Send header part
    client.print(head);

    // Send image data in chunks
    const size_t CHUNK_SIZE = 1024;
    size_t sent = 0;
    while (sent < fb->len) {
        size_t toSend = min(CHUNK_SIZE, fb->len - sent);
        client.write(fb->buf + sent, toSend);
        sent += toSend;
    }

    // Send tail
    client.print(tail);

    // Read response
    unsigned long timeout = millis();
    while (!client.available()) {
        if (millis() - timeout > 10000) {
            Serial.println("Telegram response timeout");
            client.stop();
            return false;
        }
        delay(50);
    }

    String response = client.readString();
    client.stop();

    bool success = response.indexOf("\"ok\":true") != -1;
    if (success) {
        Serial.println("Photo sent successfully");
    } else {
        Serial.println("Failed to send photo");
    }
    return success;
}

void sendMessageTelegram(const String& message) {
    if (!bot) return;
    bot->sendMessage(CHAT_ID, message, "");
}

static bool parseIntervalArg(const String& text, int& outVal) {
    int sp = text.indexOf(' ');
    if (sp < 0) return false;
    String param = text.substring(sp + 1);
    param.trim();
    if (param.length() == 0) return false;
    // Reject non-numeric tails like "10abc"
    for (unsigned i = 0; i < param.length(); i++) {
        if (!isDigit(param[i])) return false;
    }
    outVal = param.toInt();
    return true;
}

static String buildStatusMessage(const SchedulerState& state) {
    String msg = "=== 系統狀態 ===\n\n";

    msg += "📷 定期監控: ";
    msg += state.monitorEnabled ? "開啟" : "關閉";
    msg += "\n   間隔: " + String(state.monitorIntervalSec) + " 秒\n\n";

    msg += "🤖 本地人物偵測: ";
    msg += state.aiEnabled ? "開啟" : "關閉";
    msg += "\n   間隔: " + String(state.aiIntervalSec) + " 秒\n";
    msg += "   警報冷卻: ";
#if ALERT_COOLDOWN_SEC <= 0
    msg += "關閉\n\n";
#else
    msg += String(ALERT_COOLDOWN_SEC) + " 秒（config.h）\n\n";
#endif

    msg += "📶 WiFi RSSI: " + String(WiFi.RSSI()) + " dBm\n";
    msg += "💾 Free heap: " + String(ESP.getFreeHeap() / 1024) + " KB\n";
    if (psramFound()) {
        msg += "💾 Free PSRAM: " + String(ESP.getFreePsram() / 1024) + " KB\n";
    }
    return msg;
}

static String buildHelpMessage() {
    String msg = "=== ESP32-CAM 監控機器人 ===\n\n";
    msg += "📷 拍照指令:\n";
    msg += "  /photo - 立即拍照\n\n";
    msg += "🔄 定期監控:\n";
    msg += "  /monitor_on - 開啟定期拍照\n";
    msg += "  /monitor_off - 關閉定期拍照\n";
    msg += "  /monitor_interval <秒> - 設定間隔（" +
          String(MIN_MONITOR_INTERVAL_SEC) + "～" +
          String(MAX_MONITOR_INTERVAL_SEC) + "）\n\n";
    msg += "🤖 本地人物偵測 (TFLite):\n";
    msg += "  /ai_on - 開啟本地偵測\n";
    msg += "  /ai_off - 關閉本地偵測\n";
    msg += "  /ai_interval <秒> - 設定間隔（" +
          String(MIN_AI_INTERVAL_SEC) + "～" +
          String(MAX_AI_INTERVAL_SEC) + "）\n\n";
    msg += "ℹ️ 其他:\n";
    msg += "  /status - 查看系統狀態\n";
    msg += "  /start - 顯示此說明\n";
    return msg;
}

void handleTelegramMessages(SchedulerState& state) {
    if (!bot) return;

    unsigned long now = millis();
    if (now - lastBotCheck < BOT_CHECK_INTERVAL) return;
    lastBotCheck = now;

    int numNewMessages = bot->getUpdates(bot->last_message_received + 1);

    for (int i = 0; i < numNewMessages; i++) {
        String chatIdStr = bot->messages[i].chat_id;
        String text = bot->messages[i].text;

        // Privacy check: only respond to authorized chat ID
        if (chatIdStr != CHAT_ID) {
            Serial.printf("Unauthorized access from chat ID: %s\n", chatIdStr.c_str());
            continue;
        }

        Serial.printf("Received command: %s\n", text.c_str());

        if (text == "/start") {
            sendMessageTelegram(buildHelpMessage());
        }
        else if (text == "/photo") {
            sendMessageTelegram("📸 拍照中...");
            camera_fb_t* fb = capturePhoto(FRAMESIZE_SVGA);
            if (fb) {
                sendPhotoTelegram(fb, "手動拍照");
                esp_camera_fb_return(fb);
            } else {
                sendMessageTelegram("❌ 拍照失敗");
            }
        }
        else if (text == "/monitor_on") {
            state.monitorEnabled = true;
            state.lastMonitorTime = millis();
            state.monitorRunning = false;
            sendMessageTelegram("✅ 定期監控已開啟，間隔 " + String(state.monitorIntervalSec) + " 秒");
        }
        else if (text == "/monitor_off") {
            state.monitorEnabled = false;
            state.monitorRunning = false;
            sendMessageTelegram("⛔ 定期監控已關閉");
        }
        else if (text.startsWith("/monitor_interval")) {
            int val = 0;
            if (parseIntervalArg(text, val) &&
                val >= MIN_MONITOR_INTERVAL_SEC &&
                val <= MAX_MONITOR_INTERVAL_SEC) {
                state.monitorIntervalSec = val;
                sendMessageTelegram("✅ 監控間隔已設定為 " + String(val) + " 秒");
            } else {
                sendMessageTelegram(
                    "❌ 監控間隔請設 " + String(MIN_MONITOR_INTERVAL_SEC) +
                    "～" + String(MAX_MONITOR_INTERVAL_SEC) + " 秒\n"
                    "例：/monitor_interval 30");
            }
        }
        else if (text == "/ai_on") {
            state.aiEnabled = true;
            state.lastAITime = millis();
            state.aiRunning = false;
            sendMessageTelegram("✅ 本地人物偵測已開啟，間隔 " + String(state.aiIntervalSec) + " 秒");
        }
        else if (text == "/ai_off") {
            state.aiEnabled = false;
            state.aiRunning = false;
            sendMessageTelegram("⛔ 本地人物偵測已關閉");
        }
        else if (text.startsWith("/ai_interval")) {
            int val = 0;
            if (parseIntervalArg(text, val) &&
                val >= MIN_AI_INTERVAL_SEC &&
                val <= MAX_AI_INTERVAL_SEC) {
                state.aiIntervalSec = val;
                sendMessageTelegram("✅ 本地偵測間隔已設定為 " + String(val) + " 秒");
            } else {
                sendMessageTelegram(
                    "❌ 本地偵測間隔請設 " + String(MIN_AI_INTERVAL_SEC) +
                    "～" + String(MAX_AI_INTERVAL_SEC) + " 秒\n"
                    "例：/ai_interval 10");
            }
        }
        else if (text == "/status") {
            sendMessageTelegram(buildStatusMessage(state));
        }
        else {
            sendMessageTelegram("❓ 未知指令，請輸入 /start 查看可用指令");
        }
    }
}
