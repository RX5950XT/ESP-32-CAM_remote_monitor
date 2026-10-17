#include "telegram.h"
#include "camera.h"
#include "scheduler.h"
#include "device_control.h"
#include "device_identity.h"
#include <ArduinoJson.h>

static UniversalTelegramBot* bot = nullptr;
static unsigned long lastBotCheck = 0;
static const unsigned long BOT_CHECK_INTERVAL = 1000;
static int queuedMessages = 0;
static int nextMessage = 0;

static bool telegramPhotoResult(bool success) {
    deviceRecordTelegram(success);
    return success;
}

static bool writeTelegram(WiFiClientSecure& client, const uint8_t* data, size_t length) {
    const unsigned long start = millis();
    while (length && client.connected() && millis() - start < 10000) {
        const size_t written = client.write(data, length < 1024 ? length : 1024);
        data += written;
        length -= written;
        delay(1);
    }
    return length == 0;
}

void telegramInit(WiFiClientSecure& client) {
    bot = new UniversalTelegramBot(BOT_TOKEN, client);
    bot->longPoll = 0;
    Serial.println("Telegram bot initialized");
}

bool sendPhotoTelegram(camera_fb_t* fb, const char* caption) {
    if (!fb) return telegramPhotoResult(false);

    Serial.printf("Sending photo (%u bytes) to Telegram...\n", fb->len);

    WiFiClientSecure client;
    client.setInsecure();

    if (!client.connect("api.telegram.org", 443)) {
        Serial.println("Failed to connect to Telegram API");
        return telegramPhotoResult(false);
    }

    String boundary = "----ESP32CAMBoundary";
    String captionStr = deviceTag();
    if (caption && strlen(caption)) captionStr += "\n" + String(caption);

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

    if (!writeTelegram(client, (const uint8_t*)head.c_str(), head.length()) ||
        !writeTelegram(client, fb->buf, fb->len) ||
        !writeTelegram(client, (const uint8_t*)tail.c_str(), tail.length())) {
        client.stop();
        return telegramPhotoResult(false);
    }

    // Read response
    unsigned long timeout = millis();
    while (!client.available()) {
        if (millis() - timeout > 10000) {
            Serial.println("Telegram response timeout");
            client.stop();
            return telegramPhotoResult(false);
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
    return telegramPhotoResult(success);
}

void sendMessageTelegram(const String& message) {
    if (!bot) return;
    deviceRecordTelegram(bot->sendMessage(CHAT_ID, deviceTag() + "\n" + message, ""));
}

static String detectionMessage(JsonObjectConst result) {
    if (!(result["available"] | false)) return "尚無偵測紀錄\n";
    String msg = "完成時間：開機後 " + String(result["completed_uptime_sec"].as<unsigned long>()) + " 秒\n";
    if (!(result["success"] | false)) return msg + "❌ 偵測失敗：" + String(result["error"] | "未知錯誤") + "\n";
    msg += (result["person_detected"] | false) ? "👤 結果：有人\n" : "結果：沒有人\n";
    msg += "有人分數：" + String(result["person_score"].as<float>(), 3);
    msg += "；無人分數：" + String(result["no_person_score"].as<float>(), 3);
    return msg + "\n耗時：" + String(result["duration_ms"].as<unsigned long>()) + " ms\n";
}

static String buildStatusMessage(const SchedulerState& state) {
    JsonDocument doc;
    deviceStatus(doc.to<JsonObject>(), state);
    String msg = "=== 系統狀態 ===\n";
    msg += deviceConnectionMessage() + "\n\n";
    msg += "定期監控：" + String(state.monitorEnabled ? "開啟" : "關閉") + "，間隔 " + String(state.monitorIntervalSec) + " 秒\n";
    msg += "人物偵測：" + String(state.aiEnabled ? "開啟" : "關閉") + "，間隔 " + String(state.aiIntervalSec) + " 秒\n";
    msg += "最低分數：" + String(doc["person_min_score"].as<float>(), 3);
    msg += "；分數差：" + String(doc["person_score_margin"].as<float>(), 3) + "\n";
    msg += "警報冷卻：" + String(doc["alert_cooldown_sec"].as<int>()) + " 秒（0 為關閉）\n";
    msg += "冷卻剩餘：" + String(doc["alert_cooldown_remaining_sec"].as<unsigned long>()) + " 秒\n";
    msg += "傳送失敗後重試間隔：" + String(doc["alert_retry_cooldown_sec"].as<int>());
    msg += " 秒，剩餘 " + String(doc["alert_retry_remaining_sec"].as<unsigned long>()) + " 秒\n";
    msg += "\n相機：曝光／增益／白平衡皆自動\n";
    msg += "亮度 " + String(doc["brightness"].as<int>()) + "；對比 " + String(doc["contrast"].as<int>());
    msg += "；飽和度 " + String(doc["saturation"].as<int>()) + "\n";
    msg += "曝光補償 " + String(doc["exposure_level"].as<int>()) + "；增益上限代碼 " + String(doc["gain_ceiling"].as<int>());
    msg += "；JPEG 品質 " + String(doc["jpeg_quality"].as<int>()) + "\n";
    msg += "\nWiFi 訊號：" + String(doc["wifi_rssi_dbm"].as<int>()) + " dBm\n";
    msg += "剩餘記憶體：" + String(doc["free_heap_bytes"].as<unsigned long>()) + " bytes\n";
    msg += "PSRAM：" + String((doc["psram"] | false) ? "可用" : "無");
    msg += "，剩餘 " + String(doc["free_psram_bytes"].as<unsigned long>()) + " bytes\n";
    msg += "開機秒數：" + String(doc["uptime_sec"].as<unsigned long>()) + "\n";
    msg += "設定保存：" + String((doc["settings_persisted"] | false) ? "已保存" : "未保存／使用預設") + "\n";
    if (!doc["settings_error"].isNull()) msg += "設定錯誤：" + String(doc["settings_error"].as<const char*>()) + "\n";
    const char* keys[] = {"camera", "inference", "telegram", "mcp_photo", "alert"};
    const char* labels[] = {"拍照", "人物偵測", "Telegram 傳送", "MCP 圖片傳送", "人物警報"};
    msg += "\n執行紀錄（時間為開機後秒數）：\n";
    for (unsigned i = 0; i < 5; ++i) {
        JsonObjectConst health = doc["health"][keys[i]];
        msg += String(labels[i]) + "：";
        if (!(health["available"] | false)) { msg += "尚未執行\n"; continue; }
        msg += String((health["success"] | false) ? "成功" : "失敗");
        msg += "，最近嘗試 " + String(health["last_attempt_uptime_sec"].as<unsigned long>());
        msg += "，最近成功 ";
        msg += health["last_success_uptime_sec"].isNull() ? String("無") : String(health["last_success_uptime_sec"].as<unsigned long>());
        msg += "\n";
        if (!health["last_error"].isNull()) {
            msg += "  最近錯誤（" + String(health["last_error_uptime_sec"].as<unsigned long>()) + " 秒）：";
            msg += String(health["last_error"].as<const char*>()) + "\n";
        }
    }
    return msg + "\n最近偵測：\n" + detectionMessage(doc["detection"].as<JsonObjectConst>());
}

static String buildHelpMessage() {
    return "=== ESP32-CAM 監控機器人 ===\n"
        "/photo [vga|svga|xga|uxga]－拍照傳回，預設 svga\n"
        "/camera <JSON>－調畫質，例如 /camera {\"exposure_level\":1,\"jpeg_quality\":8}\n"
        "可調 brightness、contrast、saturation、exposure_level（-2～2）、gain_ceiling（0～4）、jpeg_quality（8～20）\n"
        "/monitor_on、/monitor_off－定期拍照開關\n"
        "/monitor_interval <秒>－監控間隔\n"
        "/ai_on、/ai_off－定期人物偵測開關\n"
        "/ai_interval <秒>－偵測間隔\n"
        "/monitor <JSON>－同時調整開關／間隔，例如 /monitor {\"monitor_enabled\":true,\"ai_interval_sec\":10}\n"
        "可調 monitor_enabled、ai_enabled、monitor_interval_sec、ai_interval_sec；間隔 3～86400 秒\n"
        "/detect－立即偵測一次並回傳分數，不自動發警報\n"
        "/detect_status－查最近結果與時間\n"
        "/detect_config <JSON>－調門檻與警報冷卻，例如 /detect_config {\"person_min_score\":0.7,\"person_score_margin\":0.2,\"alert_cooldown_sec\":30}\n"
        "兩個分數 0～1；冷卻 0～86400 秒，0 為關閉\n"
        "/status－完整狀態、記憶體與錯誤紀錄\n"
        "/start－此說明\n\n"
        "設定自動保存，重開機也保留開關。控制／拍照／偵測完成後等 3 秒；狀態查詢不受限。";
}

static bool intervalArgument(const String& argument, int& value) {
    if (!argument.length()) return false;
    value = 0;
    for (unsigned i = 0; i < argument.length(); ++i) {
        if (argument[i] < '0' || argument[i] > '9') return false;
        value = value * 10 + argument[i] - '0';
        if (value > 86400) return false;
    }
    return true;
}

static void configureTelegram(const char* group, JsonObjectConst args, SchedulerState& state) {
    String error;
    if (!deviceActionReady(error)) { sendMessageTelegram(error); return; }
    const bool success = deviceConfigure(group, args, state, error);
    deviceActionDone();
    sendMessageTelegram(success ? String("✅ 設定已更新並保存，重開機會保留。") : String("❌ ") + error);
}

static void handleCommand(const String& text, SchedulerState& state) {
    if (text.length() > 2048) { sendMessageTelegram("❌ 指令過長，上限 2048 bytes。"); return; }
    const int space = text.indexOf(' ');
    const String command = space < 0 ? text : text.substring(0, space);
    String argument = space < 0 ? String("") : text.substring(space + 1);
    argument.trim();
    if (command == "/start") { sendMessageTelegram(buildHelpMessage()); return; }
    if (command == "/status") { sendMessageTelegram(buildStatusMessage(state)); return; }
    if (command == "/detect_status") {
        JsonDocument result;
        deviceDetectionStatus(result.to<JsonObject>());
        sendMessageTelegram(detectionMessage(result.as<JsonObjectConst>())); return;
    }
    if (command == "/camera" || command == "/monitor" || command == "/detect_config") {
        JsonDocument args;
        if (deserializeJson(args, argument, DeserializationOption::NestingLimit(2)) || !args.is<JsonObject>()) {
            sendMessageTelegram("❌ 請提供 JSON 設定物件，輸入 /start 查看範例。"); return;
        }
        const char* group = command == "/camera" ? "camera" : command == "/monitor" ? "monitor" : "detection";
        configureTelegram(group, args.as<JsonObjectConst>(), state); return;
    }
    if (command == "/monitor_on" || command == "/monitor_off" || command == "/ai_on" || command == "/ai_off" ||
        command == "/monitor_interval" || command == "/ai_interval") {
        JsonDocument args;
        if (command == "/monitor_interval" || command == "/ai_interval") {
            int value;
            if (!intervalArgument(argument, value)) { sendMessageTelegram("❌ 間隔請填 3～86400 的整數秒數。"); return; }
            args[command == "/monitor_interval" ? "monitor_interval_sec" : "ai_interval_sec"] = value;
        } else {
            if (argument.length()) { sendMessageTelegram("❌ 此開關指令不接受參數。"); return; }
            const bool monitor = command == "/monitor_on" || command == "/monitor_off";
            args[monitor ? "monitor_enabled" : "ai_enabled"] = command == "/monitor_on" || command == "/ai_on";
        }
        configureTelegram("monitor", args.as<JsonObjectConst>(), state); return;
    }
    if (command == "/photo" || command == "/detect") {
        framesize_t resolution = FRAMESIZE_SVGA;
        if (command == "/photo" && !deviceResolution(argument.length() ? argument.c_str() : "svga", resolution)) {
            sendMessageTelegram("❌ 解析度請填 vga、svga、xga 或 uxga。"); return;
        }
        if (command == "/detect" && argument.length()) { sendMessageTelegram("❌ /detect 不接受參數。"); return; }
        String error;
        if (!deviceActionReady(error)) { sendMessageTelegram(error); return; }
        if (command == "/photo") {
            if (!deviceSendPhoto(resolution, "手動拍照", error)) sendMessageTelegram("❌ " + error);
        } else {
            deviceDetect(error);
            JsonDocument result;
            deviceDetectionStatus(result.to<JsonObject>());
            sendMessageTelegram(detectionMessage(result.as<JsonObjectConst>()));
        }
        deviceActionDone(); return;
    }
    sendMessageTelegram("❓ 未知指令，請輸入 /start 查看可用指令。");
}

bool handleTelegramMessages(SchedulerState& state) {
    if (!bot) return false;
    if (nextMessage >= queuedMessages) {
        if (millis() - lastBotCheck < BOT_CHECK_INTERVAL) return false;
        lastBotCheck = millis();
        queuedMessages = bot->getUpdates(bot->last_message_received + 1);
        nextMessage = 0;
    }
    if (nextMessage >= queuedMessages) return false;
    const auto message = bot->messages[nextMessage++];
    if (message.chat_id != CHAT_ID) { Serial.println("Unauthorized Telegram command rejected"); return true; }
    handleCommand(message.text, state);
    return true;
}
