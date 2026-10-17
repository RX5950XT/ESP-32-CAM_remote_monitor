#pragma once
#include <string>
#include <map>
#include <vector>
struct MdnsMock {
    bool beginSuccess=true,serviceSuccess=true;
    std::string hostname,instance;
    struct Service { std::string name,protocol; int port; };
    std::vector<Service> services;
    std::map<std::string,std::string> txt;
    bool begin(const char* name) { hostname=name; services.clear(); txt.clear(); return beginSuccess; }
    void setInstanceName(const char* name) { instance=name; }
    bool addService(const char* name,const char* protocol,int port) { services.push_back({name,protocol,port}); return serviceSuccess; }
    void addServiceTxt(const char*,const char*,const char* key,const char* value) { txt[key]=value; }
};
inline MdnsMock MDNS;
