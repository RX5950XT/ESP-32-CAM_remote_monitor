#pragma once
#include "Arduino.h"
#include <map>
#include <vector>
inline std::map<std::string,std::vector<uint8_t>> preferencesData;
inline bool preferencesFailOpen=false, preferencesFailSave=false, preferencesFailRead=false;
inline size_t preferencesWrites=0;
class Preferences {
    std::string prefix;
public:
    bool begin(const char* name,bool=false) { prefix=std::string(name)+"/"; return !preferencesFailOpen; }
    void end() {}
    bool isKey(const char* key) const { return preferencesData.count(prefix+key)>0; }
    size_t getBytesLength(const char* key) const { auto found=preferencesData.find(prefix+key); return found==preferencesData.end()?0:found->second.size(); }
    size_t getBytes(const char* key,void* value,size_t maximum) const {
        if(preferencesFailRead) return 0;
        auto found=preferencesData.find(prefix+key);
        if(found==preferencesData.end()) return 0;
        const size_t n=std::min(maximum,found->second.size()); memcpy(value,found->second.data(),n); return n;
    }
    size_t putBytes(const char* key,const void* value,size_t size) {
        if(preferencesFailSave) return 0;
        const auto* bytes=static_cast<const uint8_t*>(value);
        preferencesData[prefix+key]=std::vector<uint8_t>(bytes,bytes+size); ++preferencesWrites; return size;
    }
};
