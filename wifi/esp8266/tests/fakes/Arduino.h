#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#define PROGMEM
#define SERIAL_8N1 0
inline size_t strlen_P(const char* p) { return strlen(p); }
inline size_t strlcpy(char* dst,const char* src,size_t n) { size_t len=strlen(src);if(n) {size_t copy=std::min(n-1,len);memcpy(dst,src,copy);dst[copy]=0;}return len; }
inline uint32_t fakeMs=0;
inline uint32_t millis() { return fakeMs; }
inline void delay(unsigned n) { fakeMs+=n?n:1; }
class String {
public:
    std::string s;
    String()=default;String(const char* p):s(p?p:""){};String(const std::string& p):s(p){}
    String(unsigned long n):s(std::to_string(n)){};String(unsigned n):s(std::to_string(n)){}
    String(int n):s(std::to_string(n)){}
    size_t length() const {return s.size();}
    const char* c_str() const {return s.c_str();}
    bool startsWith(const char* p) const {return s.rfind(p,0)==0;}
    long toInt() const {try{return std::stol(s);}catch(...){return 0;}}
    bool operator==(const char* p) const {return s==p;}
    friend String operator+(const String&a,const String&b){return String(a.s+b.s);}
};
inline std::function<void(const uint8_t*,size_t)> serialHook;
class FakeSerial {
public:
    std::vector<uint8_t> rx;size_t pos=0;
    void setRxBufferSize(size_t){}void begin(int,int){}void setDebugOutput(bool){}
    size_t write(const uint8_t* p,size_t n){if(serialHook) serialHook(p,n);return n;}
    int available(){return int(rx.size()-pos);}
    int read(){return pos<rx.size()?rx[pos++]:-1;}
    void supply(const uint8_t* p,size_t n){rx.assign(p,p+n);pos=0;}
};
inline FakeSerial Serial;
struct FakeESP {uint32_t getFreeHeap(){return 40000;}};
inline FakeESP ESP;
inline void configTime(int,int,const char*,const char*){}
