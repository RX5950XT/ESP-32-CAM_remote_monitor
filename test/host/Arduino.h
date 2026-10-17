#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <type_traits>
class String : public std::string {
public:
    using std::string::string;
    String() = default;
    String(const std::string& value):std::string(value) {}
    template<class T, std::enable_if_t<std::is_arithmetic_v<T>,int> = 0>
    String(T value):std::string(std::to_string(value)) {}
    String(float value,unsigned char decimals):String(number(value,decimals)) {}
    String(double value,unsigned char decimals):String(number(value,decimals)) {}
    String& append(const char* value) { std::string::append(value); return *this; }
    String& append(const char* value,size_t count) { std::string::append(value,count); return *this; }
    String substring(size_t from,size_t to=std::string::npos) const { return substr(from,to==npos?npos:to-from); }
    bool startsWith(const char* prefix) const { return rfind(prefix,0)==0; }
    bool endsWith(const char* suffix) const { size_t n=strlen(suffix); return size()>=n && compare(size()-n,n,suffix)==0; }
    int indexOf(char value) const { auto p=find(value); return p==npos?-1:static_cast<int>(p); }
    int indexOf(const char* value) const { auto p=find(value); return p==npos?-1:static_cast<int>(p); }
    void trim() { auto end=find_last_not_of(" \t\r\n"); if(end==npos) { clear(); return; } erase(end+1); erase(0,find_first_not_of(" \t\r\n")); }
    long toInt() const { char* end=nullptr; return strtol(c_str(),&end,10); }
    float toFloat() const { return strtof(c_str(),nullptr); }
private:
    static std::string number(double value,unsigned char decimals) { char text[64]; snprintf(text,sizeof(text),"%.*f",decimals,value); return text; }
};
using std::min;
using std::isfinite;
inline bool isDigit(char value) { return std::isdigit(static_cast<unsigned char>(value)); }
inline uint32_t clockMs = 10000;
inline bool hasPsram = true;
inline unsigned long millis() { return clockMs; }
inline void delay(unsigned long ms) { clockMs += ms; }
inline bool psramFound() { return hasPsram; }
struct SerialMock {
    template<class... T> void printf(const char*, T...) {}
    template<class T> void println(T) {}
};
inline SerialMock Serial;
struct EspMock {
    uint64_t efuseMac = 0x0000c3b2a1286f24ULL;
    uint64_t getEfuseMac() const { return efuseMac; }
    void restart() {}
    uint32_t getFreeHeap() const { return 123456; }
    uint32_t getFreePsram() const { return hasPsram?2345678:0; }
};
inline EspMock ESP;
