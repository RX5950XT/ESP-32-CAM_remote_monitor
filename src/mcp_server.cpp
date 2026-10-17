#include "mcp_server.h"
#include "camera.h"
#include "scheduler.h"
#include "device_control.h"
#include "device_identity.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <mbedtls/base64.h>
#include <string>
#include <math.h>

#ifndef MCP_AUTH_TOKEN
#define MCP_AUTH_TOKEN ""
#endif

namespace {
WiFiServer server(80, 2);
bool enabled = false;
bool mdnsReady = false;
unsigned long lastClientMs = 0;
constexpr size_t maxBody = 2048;
constexpr unsigned long requestTimeoutMs = 1500;

const char toolsJson[] = R"json([
 {"name":"camera_status","description":"Read all shared settings, free memory, WiFi, latest detection and operation health. Automatic exposure, gain and white balance are always enabled.","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"annotations":{"readOnlyHint":true,"openWorldHint":false}},
 {"name":"camera_capture","description":"Take a fresh JPEG and return it as an MCP image, without sending it to Telegram. Wait at least 3 seconds between actions. Without PSRAM, resolution falls back to CIF.","inputSchema":{"type":"object","properties":{"resolution":{"type":"string","enum":["vga","svga","xga","uxga"],"default":"svga"}},"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false,"idempotentHint":false,"openWorldHint":false}},
 {"name":"camera_configure","description":"Update and automatically save image settings across reboot. Exposure, gain and white balance stay automatic. jpeg_quality: smaller is clearer but larger; gain_ceiling: 0=2x,1=4x,2=8x,3=16x,4=32x (more noise). Wait 3 seconds between actions.","inputSchema":{"type":"object","properties":{"brightness":{"type":"integer","minimum":-2,"maximum":2},"contrast":{"type":"integer","minimum":-2,"maximum":2},"saturation":{"type":"integer","minimum":-2,"maximum":2},"exposure_level":{"type":"integer","minimum":-2,"maximum":2},"gain_ceiling":{"type":"integer","minimum":0,"maximum":4},"jpeg_quality":{"type":"integer","minimum":8,"maximum":20}},"minProperties":1,"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false,"idempotentHint":true,"openWorldHint":false}},
 {"name":"monitor_configure","description":"Enable/disable periodic Telegram photos or local person detection; intervals 3..86400 seconds. Automatically saved, including enabled switches, and restored on reboot. Wait 3 seconds between actions.","inputSchema":{"type":"object","properties":{"monitor_enabled":{"type":"boolean"},"ai_enabled":{"type":"boolean"},"monitor_interval_sec":{"type":"integer","minimum":3,"maximum":86400},"ai_interval_sec":{"type":"integer","minimum":3,"maximum":86400}},"minProperties":1,"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false,"idempotentHint":true,"openWorldHint":true}},
 {"name":"detection_run","description":"Run local person detection once and return success, person/no-person scores, decision, duration and uptime. Does not enable periodic AI or send an alert. Failure is distinct from no person. Wait 3 seconds between actions.","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false,"idempotentHint":false,"openWorldHint":false}},
 {"name":"detection_status","description":"Read the latest detection attempt, scores, decision, duration, completion uptime and error. No new inference. Results clear on reboot; available=false before the first run.","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"annotations":{"readOnlyHint":true,"openWorldHint":false}},
 {"name":"detection_configure","description":"Update and automatically save person score thresholds (0..1) and alert cooldown seconds (0..86400, 0 disables cooldown). Shared with Telegram and scheduled AI. Wait 3 seconds between actions.","inputSchema":{"type":"object","properties":{"person_min_score":{"type":"number","minimum":0,"maximum":1},"person_score_margin":{"type":"number","minimum":0,"maximum":1},"alert_cooldown_sec":{"type":"integer","minimum":0,"maximum":86400}},"minProperties":1,"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false,"idempotentHint":true,"openWorldHint":false}},
 {"name":"camera_send_telegram","description":"Take one fresh photo and send it to the original authorized Telegram chat. Returns delivery status, not image content. resolution defaults to svga; without PSRAM falls back to CIF. Wait 3 seconds between actions.","inputSchema":{"type":"object","properties":{"resolution":{"type":"string","enum":["vga","svga","xga","uxga"],"default":"svga"}},"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false,"idempotentHint":false,"openWorldHint":true}}
])json";

bool writeAll(WiFiClient& client, const char* data, size_t length) {
    const unsigned long start = millis();
    while (length && client.connected() && millis() - start < 5000) {
        const size_t written = client.write((const uint8_t*)data, length);
        data += written;
        length -= written;
        delay(1);
    }
    return length == 0;
}

void sendHeaders(WiFiClient& client, int status, size_t length) {
    char headers[256];
    snprintf(headers, sizeof(headers),
             "HTTP/1.1 %d Response\r\nContent-Type: application/json\r\n"
             "Content-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n",
             status, (unsigned)length);
    writeAll(client, headers, strlen(headers));
}

void sendHttp(WiFiClient& client, int status, const char* error) {
    JsonDocument doc;
    doc["error"] = error;
    std::string body;
    serializeJson(doc, body);
    sendHeaders(client, status, body.size());
    writeAll(client, body.c_str(), body.size());
}

void sendJson(WiFiClient& client, const JsonDocument& doc) {
    std::string body;
    serializeJson(doc, body);
    sendHeaders(client, 200, body.size());
    writeAll(client, body.c_str(), body.size());
}

void rpcError(JsonDocument& reply, int code, const char* message) {
    reply.remove("result");
    reply["error"]["code"] = code;
    reply["error"]["message"] = message;
}

void toolError(JsonDocument& reply, const char* message) {
    reply["result"]["isError"] = true;
    reply["result"]["content"][0]["type"] = "text";
    reply["result"]["content"][0]["text"] = message;
}

void addStructuredText(JsonDocument& reply) {
    std::string text;
    serializeJson(reply["result"]["structuredContent"], text);
    reply["result"]["content"][0]["type"] = "text";
    reply["result"]["content"][0]["text"] = text;
}

void addStatus(JsonDocument& reply, const SchedulerState& state) {
    deviceStatus(reply["result"]["structuredContent"].to<JsonObject>(), state);
    addStructuredText(reply);
}

void addDetection(JsonDocument& reply) {
    deviceDetectionStatus(reply["result"]["structuredContent"].to<JsonObject>());
    addStructuredText(reply);
}

bool knownVersion(const std::string& version) {
    return version == "2025-03-26" || version == "2025-06-18" || version == "2025-11-25";
}

// Cap each line, the total headers and elapsed time before allocating a JSON body.
bool readLine(WiFiClient& client, std::string& line, size_t& remaining, unsigned long start) {
    line.clear();
    while (remaining && line.size() < 512 && millis() - start < requestTimeoutMs) {
        if (!client.connected()) return false;
        if (!client.available()) { delay(1); continue; }
        const int c = client.read();
        --remaining;
        if (c == '\n') {
            if (line.empty() || line.back() != '\r') return false;
            line.pop_back();
            return true;
        }
        line += (char)c;
    }
    return false;
}

bool tokenMatches(const std::string& authorization) {
    const std::string expected = std::string("Bearer ") + MCP_AUTH_TOKEN;
    if (authorization.size() != expected.size()) return false;
    volatile unsigned char difference = 0;
    for (size_t i = 0; i < expected.size(); ++i) difference |= authorization[i] ^ expected[i];
    return difference == 0;
}

bool readRequest(WiFiClient& client, std::string& body) {
    const unsigned long start = millis();
    size_t remaining = 2048;
    std::string line, method, host, origin, auth, type, version, length;
    if (!readLine(client, line, remaining, start)) { sendHttp(client, 400, "Invalid request line"); return false; }
    const size_t space = line.find(' ');
    if (space == std::string::npos || line.substr(space) != " /mcp HTTP/1.1") {
        sendHttp(client, 404, "Use /mcp with HTTP/1.1"); return false;
    }
    method = line.substr(0, space);
    while (true) {
        if (!readLine(client, line, remaining, start)) { sendHttp(client, 431, "Headers too large or incomplete"); return false; }
        if (line.empty()) break;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) { sendHttp(client, 400, "Invalid header"); return false; }
        std::string key = line.substr(0, colon);
        for (char& c : key) c = (char)tolower((unsigned char)c);
        size_t valueStart = line.find_first_not_of(" \t", colon + 1);
        std::string value = valueStart == std::string::npos ? "" : line.substr(valueStart);
        const size_t end = value.find_last_not_of(" \t");
        if (end != std::string::npos) value.resize(end + 1);
        std::string* target = nullptr;
        if (key == "host") target = &host;
        else if (key == "origin") target = &origin;
        else if (key == "authorization") target = &auth;
        else if (key == "content-type") target = &type;
        else if (key == "content-length") target = &length;
        else if (key == "mcp-protocol-version") target = &version;
        else if (key == "transfer-encoding") { sendHttp(client, 400, "Content-Length required; chunked uploads unsupported"); return false; }
        if (target) {
            if (!target->empty()) { sendHttp(client, 400, "Duplicate header"); return false; }
            *target = value;
        }
    }
    const std::string ip = WiFi.localIP().toString().c_str();
    const std::string hostname = std::string(deviceHostname().c_str()) + ".local";
    if (host != ip && host != ip + ":80" && host != hostname && host != hostname + ":80") {
        sendHttp(client, 403, "Invalid Host"); return false;
    }
    if (!origin.empty() && origin != "http://" + ip && origin != "http://" + ip + ":80" &&
        origin != "http://" + hostname && origin != "http://" + hostname + ":80") {
        sendHttp(client, 403, "Invalid Origin"); return false;
    }
    if (!tokenMatches(auth)) { sendHttp(client, 401, "Bearer token required"); return false; }
    if (!version.empty() && !knownVersion(version)) { sendHttp(client, 400, "Unsupported MCP-Protocol-Version"); return false; }
    if (method != "POST") { sendHttp(client, 405, "Use POST; SSE and sessions are not provided"); return false; }
    if (type != "application/json" && type != "application/json; charset=utf-8") {
        sendHttp(client, 415, "Use application/json"); return false;
    }
    size_t count = 0;
    for (char c : length) {
        if (c < '0' || c > '9') { sendHttp(client, 400, "Invalid Content-Length"); return false; }
        count = count * 10 + (c - '0');
        if (count > maxBody) { sendHttp(client, 413, "Body exceeds 2048 bytes"); return false; }
    }
    if (!count) { sendHttp(client, 400, "Nonempty Content-Length required"); return false; }
    body.reserve(count);
    while (body.size() < count && millis() - start < requestTimeoutMs) {
        if (!client.connected()) break;
        if (client.available()) body += (char)client.read();
        else delay(1);
    }
    if (body.size() != count) { sendHttp(client, 408, "Incomplete body"); return false; }
    return true;
}

bool validateArguments(const char* name, JsonObjectConst args) {
    JsonDocument tools;
    if (deserializeJson(tools, toolsJson)) return false;
    for (JsonObjectConst tool : tools.as<JsonArrayConst>()) {
        if (strcmp(tool["name"], name)) continue;
        JsonObjectConst schema = tool["inputSchema"];
        if (args.size() < (schema["minProperties"] | 0U)) return false;
        for (JsonPairConst arg : args) {
            JsonObjectConst property = schema["properties"][arg.key()];
            const char* type = property["type"] | "";
            if (!strcmp(type, "integer")) {
                if (!arg.value().is<int>() || arg.value().is<bool>()) return false;
                const int value = arg.value();
                if (value < property["minimum"].as<int>() || value > property["maximum"].as<int>()) return false;
            } else if (!strcmp(type, "number")) {
                if (!arg.value().is<double>() || arg.value().is<bool>()) return false;
                const double value = arg.value();
                if (!isfinite(value) || value < property["minimum"].as<double>() || value > property["maximum"].as<double>()) return false;
            } else if (!strcmp(type, "boolean")) {
                if (!arg.value().is<bool>()) return false;
            } else if (!strcmp(type, "string")) {
                if (!arg.value().is<const char*>()) return false;
                bool found = false;
                for (JsonVariantConst option : property["enum"].as<JsonArrayConst>()) found |= option == arg.value();
                if (!found) return false;
            } else return false;
        }
        return true;
    }
    return false;
}

bool sendPhoto(WiFiClient& client, JsonDocument& reply, framesize_t resolution) {
    camera_fb_t* fb = deviceCapture(resolution);
    if (!fb) { toolError(reply, "Camera capture failed; retry after 3 seconds"); return false; }
    std::string id;
    serializeJson(reply["id"], id);
    JsonDocument identity;
    deviceIdentityStatus(identity.to<JsonObject>());
    std::string metadata;
    serializeJson(identity, metadata);
    const std::string prefix = "{\"jsonrpc\":\"2.0\",\"id\":" + id +
        ",\"result\":{\"structuredContent\":" + metadata +
        ",\"content\":[{\"type\":\"image\",\"mimeType\":\"image/jpeg\",\"data\":\"";
    const char suffix[] = "\"}]}}";
    sendHeaders(client, 200, prefix.size() + ((fb->len + 2) / 3) * 4 + strlen(suffix));
    bool ok = writeAll(client, prefix.c_str(), prefix.size());
    const unsigned long start = millis();
    unsigned char encoded[1025];
    // 768 is divisible by 3: only the final base64 chunk gets padding.
    for (size_t offset = 0; ok && offset < fb->len; offset += 768) {
        if (millis() - start > 15000) { ok = false; break; }
        const size_t count = fb->len - offset < 768 ? fb->len - offset : 768;
        size_t written = 0;
        ok = mbedtls_base64_encode(encoded, sizeof(encoded), &written, fb->buf + offset, count) == 0;
        if (ok) ok = writeAll(client, (const char*)encoded, written);
    }
    if (ok) ok = writeAll(client, suffix, strlen(suffix));
    esp_camera_fb_return(fb);
    deviceRecordMcpPhoto(ok);
    if (!ok) Serial.println("MCP photo transfer interrupted");
    return true;
}

bool callTool(WiFiClient& client, JsonDocument& reply, JsonObjectConst params, SchedulerState& state) {
    const char* name = params["name"] | "";
    if (!params["arguments"].isUnbound() && !params["arguments"].is<JsonObjectConst>()) {
        rpcError(reply, -32602, "arguments must be an object"); return false;
    }
    JsonObjectConst args = params["arguments"];
    if (!validateArguments(name, args)) { rpcError(reply, -32602, "Unknown tool or invalid arguments; see tools/list"); return false; }
    if (!strcmp(name, "camera_status")) { addStatus(reply, state); return false; }
    if (!strcmp(name, "detection_status")) { addDetection(reply); return false; }
    String error;
    if (!deviceActionReady(error)) { toolError(reply, error.c_str()); return false; }
    // Count from completion so long captures cannot bypass the cooldown.
    bool sent = false;
    if (!strcmp(name, "camera_capture") || !strcmp(name, "camera_send_telegram")) {
        framesize_t resolution;
        deviceResolution(args["resolution"] | "svga", resolution);
        if (!strcmp(name, "camera_capture")) sent = sendPhoto(client, reply, resolution);
        else if (deviceSendPhoto(resolution, "MCP 手動拍照", error)) {
            deviceIdentityStatus(reply["result"]["structuredContent"].to<JsonObject>());
            reply["result"]["structuredContent"]["success"] = true;
            reply["result"]["structuredContent"]["sent_to_telegram"] = true;
            addStructuredText(reply);
        } else toolError(reply, error.c_str());
    } else if (!strcmp(name, "detection_run")) {
        const bool success = deviceDetect(error);
        addDetection(reply);
        if (!success) reply["result"]["isError"] = true;
    } else {
        const char* group = !strcmp(name, "camera_configure") ? "camera" :
                            !strcmp(name, "monitor_configure") ? "monitor" : "detection";
        if (deviceConfigure(group, args, state, error)) addStatus(reply, state);
        else toolError(reply, error.c_str());
    }
    deviceActionDone();
    return sent;
}

void dispatch(WiFiClient& client, const std::string& body, SchedulerState& state) {
    JsonDocument request, reply;
    reply["jsonrpc"] = "2.0";
    reply["id"] = nullptr;
    if (deserializeJson(request, body, DeserializationOption::NestingLimit(8))) {
        rpcError(reply, -32700, "Invalid JSON"); sendJson(client, reply); return;
    }
    const bool hasId = !request["id"].isUnbound();
    if (!request.is<JsonObject>() || request["jsonrpc"] != "2.0" || !request["method"].is<const char*>() ||
        (hasId && !request["id"].is<const char*>() && !request["id"].is<long long>()) ||
        (!request["params"].isNull() && !request["params"].is<JsonObjectConst>())) {
        rpcError(reply, -32600, "Invalid JSON-RPC request"); sendJson(client, reply); return;
    }
    const char* method = request["method"];
    if (!hasId) {
        if (!strncmp(method, "notifications/", 14)) sendHeaders(client, 202, 0);
        else sendHttp(client, 400, "Requests require an id");
        return;
    }
    reply["id"] = request["id"];
    if (!strcmp(method, "initialize")) {
        if (!request["params"]["protocolVersion"].is<const char*>()) rpcError(reply, -32602, "protocolVersion required");
        else {
            const std::string version = request["params"]["protocolVersion"].as<const char*>();
            reply["result"]["protocolVersion"] = knownVersion(version) ? version : "2025-11-25";
            reply["result"]["capabilities"]["tools"]["listChanged"] = false;
            reply["result"]["serverInfo"]["name"] = deviceHostname();
            reply["result"]["serverInfo"]["version"] = "1.1.0";
        }
    } else if (!strcmp(method, "ping")) reply["result"].to<JsonObject>();
    else if (!strcmp(method, "tools/list")) {
        JsonDocument tools;
        deserializeJson(tools, toolsJson);
        reply["result"]["tools"] = tools;
    } else if (!strcmp(method, "tools/call")) {
        if (callTool(client, reply, request["params"], state)) return;
    } else rpcError(reply, -32601, "Method not found");
    sendJson(client, reply);
}
} // namespace

void mcpInit() {
    enabled = mdnsReady = false;
    if (strlen(MCP_AUTH_TOKEN) < 32) {
        Serial.println("MCP disabled: configure MCP_AUTH_TOKEN (at least 32 characters)");
        return;
    }
    server.begin();
    enabled = true;
    const String hostname = deviceHostname();
    mdnsReady = MDNS.begin(hostname.c_str());
    if (mdnsReady) {
        MDNS.setInstanceName(hostname.c_str());
        if (!MDNS.addService("http", "tcp", 80)) Serial.println("mDNS service advertisement failed");
        MDNS.addServiceTxt("http", "tcp", "path", "/mcp");
        MDNS.addServiceTxt("http", "tcp", "id", deviceId().c_str());
        MDNS.addServiceTxt("http", "tcp", "name", deviceDisplayName().c_str());
    } else Serial.println("mDNS initialization failed; MCP remains available by IP");
}

bool mcpEnabled() { return enabled; }
bool mcpMdnsReady() { return mdnsReady; }

bool handleMcpClient(SchedulerState& state) {
    if (!enabled) return false;
    // Leave loop time for Telegram and scheduled work even under a request flood.
    if (millis() - lastClientMs < 100) return false;
    WiFiClient client = server.available();
    if (!client) return false;
    const uint32_t mask = (uint32_t)WiFi.subnetMask();
    if (((uint32_t)client.remoteIP() & mask) != ((uint32_t)WiFi.localIP() & mask)) sendHttp(client, 403, "Same-subnet clients only");
    else {
        std::string body;
        if (readRequest(client, body)) dispatch(client, body, state);
    }
    client.stop();
    lastClientMs = millis();
    return true;
}
