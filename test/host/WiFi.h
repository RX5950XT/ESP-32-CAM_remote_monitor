#pragma once
#include "Arduino.h"
#include <algorithm>
#include <memory>
struct IPAddress {
    uint32_t value;
    explicit IPAddress(uint32_t v=0):value(v) {}
    operator uint32_t() const { return value; }
    String toString() const { return "192.168.2.50"; }
};
struct SocketMock {
    std::string input,output;
    size_t position=0;
    bool open=true;
    uint32_t remote=0xc0a80202;
};
class WiFiClient {
public:
    std::shared_ptr<SocketMock> socket;
    WiFiClient(std::shared_ptr<SocketMock> s=nullptr):socket(s) {}
    explicit operator bool() const { return socket != nullptr; }
    bool connected() const { return socket && socket->open; }
    int available() const { return socket->input.size()-socket->position; }
    int read() { return (uint8_t)socket->input[socket->position++]; }
    size_t write(const uint8_t* data,size_t size) {
        if (!connected()) return 0;
        const size_t count=std::min(size,size_t(111)); // Exercise partial socket writes.
        socket->output.append((const char*)data,count);
        return count;
    }
    void stop() { if(socket) socket->open=false; }
    IPAddress remoteIP() const { return IPAddress(socket->remote); }
};
inline std::shared_ptr<SocketMock> pendingSocket;
class WiFiServer {
public:
    WiFiServer(int,int) {}
    void begin() {}
    WiFiClient available() { auto s=pendingSocket; pendingSocket.reset(); return WiFiClient(s); }
};
struct WiFiMock {
    IPAddress localIP() { return IPAddress(0xc0a80232); }
    IPAddress subnetMask() { return IPAddress(0xffffff00); }
    int RSSI() { return -55; }
};
inline WiFiMock WiFi;
