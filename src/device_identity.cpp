#include "device_identity.h"
#include "config.h"
#include "mcp_server.h"
#include <WiFi.h>

#ifndef DEVICE_NAME
#define DEVICE_NAME ""
#endif

String deviceId() {
    const uint64_t mac = ESP.getEfuseMac();
    char id[13];
    // Arduino stores the first MAC byte in the least significant byte.
    snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x",
             unsigned(mac & 0xff), unsigned((mac >> 8) & 0xff),
             unsigned((mac >> 16) & 0xff), unsigned((mac >> 24) & 0xff),
             unsigned((mac >> 32) & 0xff), unsigned((mac >> 40) & 0xff));
    return id;
}

String deviceHostname() { return "esp32cam-" + deviceId(); }

String deviceDisplayName() {
    return strlen(DEVICE_NAME) ? String(DEVICE_NAME) : deviceHostname();
}

String deviceTag() {
    const String name = deviceDisplayName();
    const String host = deviceHostname();
    return "[" + name + (name == host ? String("") : " / " + host) + "]";
}

String deviceMcpUrl() { return "http://" + deviceHostname() + ".local/mcp"; }

String deviceConnectionMessage() {
    String message = "設備 ID：" + deviceId() + "\n主機名稱：" + deviceHostname() + ".local";
    message += "\nIP：" + WiFi.localIP().toString();
    if (!mcpEnabled()) return message + "\nMCP：未啟用";
    if (mcpMdnsReady()) message += "\nMCP：" + deviceMcpUrl();
    else message += "\nmDNS 未啟用，請使用 IP 連線";
    return message + "\nMCP（IP）：http://" + WiFi.localIP().toString() + "/mcp";
}

void deviceIdentityStatus(JsonObject data) {
    data["device_id"] = deviceId();
    data["device_name"] = deviceDisplayName();
    data["hostname"] = deviceHostname() + ".local";
    data["ip_address"] = WiFi.localIP().toString();
    data["mcp_enabled"] = mcpEnabled();
    data["mdns_ready"] = mcpMdnsReady();
    data["mcp_url"] = nullptr;
    data["mcp_ip_url"] = nullptr;
    if (mcpEnabled()) {
        if (mcpMdnsReady()) data["mcp_url"] = deviceMcpUrl();
        data["mcp_ip_url"] = "http://" + WiFi.localIP().toString() + "/mcp";
    }
}
