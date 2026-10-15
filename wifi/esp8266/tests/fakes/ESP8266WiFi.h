#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
#define WIFI_STA 1
#define WIFI_OFF 0
class WiFiClient {
public:
    std::string input;size_t cursor=0;bool stopped=false;
    int available(){return int(input.size()-cursor);}
    int read(uint8_t* p,size_t n){n=std::min(n,input.size()-cursor);memcpy(p,input.data()+cursor,n);cursor+=n;return int(n);}
    bool connected(){return !stopped;}
    void stop(){stopped=true;}
};
struct FakeWiFi {
    int connection=WL_CONNECTED;
    int status(){return connection;}int RSSI(){return -60;}
    void persistent(bool){}void mode(int){}void setAutoReconnect(bool){}
    void begin(const char*,const char*){}void reconnect(){}
};
inline FakeWiFi WiFi;
