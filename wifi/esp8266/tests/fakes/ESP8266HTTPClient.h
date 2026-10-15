#pragma once
#include "ESP8266WiFi.h"
#define HTTPC_DISABLE_FOLLOW_REDIRECTS 0
struct HttpResult {
    int code=200;std::string body="{}",contentType="application/json",encoding="",transfer="",retry="";
    int declared=-2;
};
inline std::function<HttpResult(const std::string&,const std::string&,const std::string&)> httpHook;
class HTTPClient {
    WiFiClient* socket=nullptr;String path;HttpResult response;
public:
    void setTimeout(int){}void setReuse(bool){}void setFollowRedirects(int){}
    bool begin(WiFiClient& c,const String& p){socket=&c;path=p;return true;}
    void collectHeaders(const char**,int){}void addHeader(const char*,const String&){}
    int sendRequest(const char* method,uint8_t* body,size_t n){
        response=httpHook(method,path.s,body?std::string(reinterpret_cast<char*>(body),n):"");
        socket->input=response.body;socket->cursor=0;socket->stopped=false;return response.code;
    }
    int getSize(){return response.declared==-2?int(response.body.size()):response.declared;}
    String header(const char* h){
        if(!strcmp(h,"Content-Type"))return response.contentType;
        if(!strcmp(h,"Transfer-Encoding"))return response.transfer;
        if(!strcmp(h,"Content-Encoding"))return response.encoding;
        if(!strcmp(h,"Retry-After"))return response.retry;
        return "";
    }
    WiFiClient* getStreamPtr(){return socket;}void end(){}
};
