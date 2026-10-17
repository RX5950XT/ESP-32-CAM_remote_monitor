#pragma once
#include "WiFi.h"
#include <vector>
inline bool telegramConnectSuccess=true, telegramMessageSuccess=true;
inline String telegramHttpResponse="HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n{\"ok\":true}";
inline std::vector<String> telegramRequests;
inline size_t telegramWriteLimit=1024;
class WiFiClientSecure {
    String request;
public:
    void setInsecure() {}
    void setTimeout(unsigned long) {}
    bool connect(const char*,int) { return telegramConnectSuccess; }
    bool connected() const { return true; }
    int available() const { return telegramHttpResponse.size(); }
    size_t write(const uint8_t* data,size_t length) { auto n=min(length,telegramWriteLimit); request.append(reinterpret_cast<const char*>(data),n); return n; }
    void print(const String& value) { request+=value; }
    void println(const String& value="") { request+=value; request+="\r\n"; }
    String readString() const { return telegramHttpResponse; }
    void stop() { telegramRequests.push_back(request); request.clear(); }
};
