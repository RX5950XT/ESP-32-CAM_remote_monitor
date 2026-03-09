#include "ai_monitor.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"

static const char* OPENROUTER_HOST = "openrouter.ai";
static const int OPENROUTER_PORT = 443;
static const char* OPENROUTER_PATH = "/api/v1/chat/completions";

static char* base64Encode(const uint8_t* data, size_t inputLen, size_t* outputLen) {
    // Calculate required output buffer size
    size_t requiredLen = 0;
    mbedtls_base64_encode(nullptr, 0, &requiredLen, data, inputLen);

    // Allocate in PSRAM if available
    char* output = nullptr;
    if (psramFound()) {
        output = (char*)ps_malloc(requiredLen + 1);
    } else {
        output = (char*)malloc(requiredLen + 1);
    }

    if (!output) {
        Serial.println("Failed to allocate base64 buffer");
        return nullptr;
    }

    size_t actualLen = 0;
    int ret = mbedtls_base64_encode(
        (unsigned char*)output, requiredLen + 1, &actualLen,
        data, inputLen);

    if (ret != 0) {
        Serial.printf("Base64 encode failed: %d\n", ret);
        free(output);
        return nullptr;
    }

    output[actualLen] = '\0';
    *outputLen = actualLen;
    return output;
}

bool analyzePhotoForHuman(camera_fb_t* fb) {
    if (!fb) return false;

    Serial.printf("AI analysis: encoding %u bytes...\n", fb->len);

    // Base64 encode the JPEG
    size_t base64Len = 0;
    char* base64Data = base64Encode(fb->buf, fb->len, &base64Len);
    if (!base64Data) return false;

    Serial.printf("Base64 encoded: %u bytes\n", base64Len);

    // Build the data URI
    String dataUri = "data:image/jpeg;base64,";
    // We'll stream the JSON manually to avoid massive String concatenation

    // Build JSON payload
    // Using streaming approach to minimize memory usage
    WiFiClientSecure client;
    client.setInsecure();

    if (!client.connect(OPENROUTER_HOST, OPENROUTER_PORT)) {
        Serial.println("Failed to connect to OpenRouter");
        free(base64Data);
        return false;
    }

    // Construct the JSON body parts
    String jsonPrefix = "{\"model\":\"" + String(OPENROUTER_MODEL) + "\","
        "\"messages\":[{\"role\":\"user\",\"content\":["
        "{\"type\":\"text\",\"text\":\"這張照片中有沒有人？只回答『有』或『沒有』，不要有其他文字。\"},"
        "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
    String jsonSuffix = "\"}}]}],\"max_tokens\":10}";

    size_t contentLength = jsonPrefix.length() + base64Len + jsonSuffix.length();

    // Send HTTP request headers
    client.println("POST " + String(OPENROUTER_PATH) + " HTTP/1.1");
    client.println("Host: " + String(OPENROUTER_HOST));
    client.println("Authorization: Bearer " + String(OPENROUTER_API_KEY));
    client.println("Content-Type: application/json");
    client.println("Content-Length: " + String(contentLength));
    client.println("Connection: close");
    client.println();

    // Send body in chunks to avoid huge String allocation
    client.print(jsonPrefix);

    // Send base64 data in chunks
    const size_t CHUNK_SIZE = 1024;
    size_t sent = 0;
    while (sent < base64Len) {
        size_t toSend = min(CHUNK_SIZE, base64Len - sent);
        client.write((const uint8_t*)(base64Data + sent), toSend);
        sent += toSend;
    }

    client.print(jsonSuffix);

    // Free base64 buffer immediately
    free(base64Data);
    base64Data = nullptr;

    Serial.println("AI request sent, waiting for response...");

    // Read response
    unsigned long timeout = millis();
    while (!client.available()) {
        if (millis() - timeout > 30000) {
            Serial.println("AI response timeout");
            client.stop();
            return false;
        }
        delay(100);
    }

    // Skip HTTP headers
    bool headersEnded = false;
    String responseLine;
    while (client.available() && !headersEnded) {
        responseLine = client.readStringUntil('\n');
        if (responseLine == "\r") {
            headersEnded = true;
        }
    }

    // Read response body
    String responseBody = "";
    while (client.available()) {
        responseBody += client.readStringUntil('\n');
    }
    client.stop();

    Serial.printf("AI response: %s\n", responseBody.c_str());

    // Parse JSON response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, responseBody);
    if (error) {
        Serial.printf("JSON parse failed: %s\n", error.c_str());
        return false;
    }

    String content = doc["choices"][0]["message"]["content"] | "";
    content.toLowerCase();

    Serial.printf("AI answer: %s\n", content.c_str());

    // Check if the model detected a person
    bool personDetected = content.indexOf("有") != -1 && content.indexOf("沒有") == -1;

    // Also check for alternative positive responses
    if (!personDetected) {
        personDetected = content.indexOf("yes") != -1;
    }

    return personDetected;
}
