#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

String deviceId();
String deviceHostname();
String deviceDisplayName();
String deviceTag();
String deviceMcpUrl();
String deviceConnectionMessage();
void deviceIdentityStatus(JsonObject data);
