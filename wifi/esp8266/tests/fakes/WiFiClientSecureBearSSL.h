#pragma once
#include "ESP8266WiFi.h"
namespace BearSSL {
struct Session{};struct X509List{explicit X509List(const char*){}};
struct WiFiClientSecure:WiFiClient {
    void setTrustAnchors(X509List*){}void setSession(Session*){}
    void setHandshakeTimeout(int){}void setTimeout(int){}
};
}
