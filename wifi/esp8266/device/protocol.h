#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace wire {
constexpr size_t MaxPayload = 544, MaxRaw = 562, MaxEncoded = 565, MaxWire = 567;
enum Type : uint8_t { Hello=1, Status=2, Command=3, Result=4, Time=5,
                     PhotoNext=6, PhotoRead=7, PhotoCommit=8, Error=255 };
inline uint16_t u16(const uint8_t* p) { return p[0] | uint16_t(p[1])<<8; }
inline uint32_t u32(const uint8_t* p) { return u16(p) | uint32_t(u16(p+2))<<16; }
inline uint64_t u64(const uint8_t* p) { return u32(p) | uint64_t(u32(p+4))<<32; }
inline void put16(uint8_t* p, uint16_t v) { p[0]=v; p[1]=v>>8; }
inline void put32(uint8_t* p, uint32_t v) { put16(p,v); put16(p+2,v>>16); }
inline void put64(uint8_t* p, uint64_t v) { put32(p,v); put32(p+4,v>>32); }
inline bool elapsed(uint32_t now, uint32_t then, uint32_t ms) { return uint32_t(now-then)>=ms; }
inline uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c=0xffffffffU;
    while (n--) { c^=*p++; for (unsigned b=0;b<8;b++) c=(c>>1)^((0U-(c&1U))&0xedb88320U); }
    return c^0xffffffffU;
}
inline int nibble(char c) { return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1; }
inline bool unhex(const char* s, uint8_t* dst, size_t bytes) {
    if (!s || strlen(s)!=bytes*2) return false;
    for (size_t i=0;i<bytes;i++) { int a=nibble(s[2*i]),b=nibble(s[2*i+1]);
        if (a<0||b<0) return false;
        dst[i]=uint8_t(a*16+b); }
    return true;
}
inline void hex(const uint8_t* p, size_t n, char* dst) {
    const char* alphabet="0123456789abcdef";
    for (size_t i=0;i<n;i++) { dst[2*i]=alphabet[p[i]>>4]; dst[2*i+1]=alphabet[p[i]&15]; }
    dst[2*n]=0;
}
inline bool zeroId(const uint8_t* p) { uint8_t v=0; for(unsigned i=0;i<16;i++) v|=p[i]; return v==0; }
inline bool interval(uint32_t n) { return n==2||n==5||n==10||n==30||n==60; }

inline size_t cobsEncode(const uint8_t* src, size_t n, uint8_t* dst, size_t cap) {
    if (!cap) return 0;
    size_t pos=1,mark=0; uint8_t code=1;
    for(size_t i=0;i<n;i++) {
        if(src[i]==0) { dst[mark]=code; if(pos>=cap) return 0; mark=pos++; code=1; }
        else { if(pos>=cap) return 0; dst[pos++]=src[i];
            if(++code==255) { dst[mark]=code; if(pos>=cap) return 0; mark=pos++; code=1; } }
    }
    dst[mark]=code; return pos;
}
inline size_t cobsDecode(const uint8_t* src,size_t n,uint8_t* dst,size_t cap) {
    size_t r=0,w=0;
    while(r<n) {
        uint8_t code=src[r++]; if(!code || size_t(code-1)>n-r) return 0;
        for(unsigned j=1;j<code;j++) { if(w>=cap || src[r]==0) return 0; dst[w++]=src[r++]; }
        if(code!=255 && r<n) { if(w>=cap) return 0; dst[w++]=0; }
    }
    return w;
}
struct Frame { uint8_t type=0; uint32_t session=0,seq=0; uint16_t length=0; uint8_t data[MaxPayload]{}; };
inline size_t encode(const Frame& f,uint8_t* out,size_t cap) {
    if(f.length>MaxPayload || cap<MaxWire) return 0;
    uint8_t raw[MaxRaw]; raw[0]=0x57;raw[1]=0x46;raw[2]=1;raw[3]=f.type;
    put32(raw+4,f.session);put32(raw+8,f.seq);put16(raw+12,f.length);
    memcpy(raw+14,f.data,f.length);put32(raw+14+f.length,crc32(raw,14+f.length));
    out[0]=0; size_t n=cobsEncode(raw,18+f.length,out+1,cap-2);
    if(!n) return 0;
    out[n+1]=0;return n+2;
}
class Decoder {
    uint8_t encoded[MaxEncoded]{}; size_t used=0; bool dropping=false;
    uint32_t last=0;
public:
    void reset() { used=0;dropping=false; }
    bool feed(uint8_t b,uint32_t now,Frame& f) {
        if(used && elapsed(now,last,250)) { used=0;dropping=true; }
        last=now;
        if(b) {
            if(dropping) return false;
            if(used>=MaxEncoded) { used=0;dropping=true;return false; }
            encoded[used++]=b; return false;
        }
        if(dropping || !used) { reset(); return false; }
        uint8_t raw[MaxRaw]; size_t n=cobsDecode(encoded,used,raw,sizeof(raw)); reset();
        if(n<18 || raw[0]!=0x57 || raw[1]!=0x46 || raw[2]!=1) return false;
        uint16_t len=u16(raw+12);
        if(len>MaxPayload || n!=size_t(18+len) || u32(raw+14+len)!=crc32(raw,14+len)) return false;
        f.type=raw[3];f.session=u32(raw+4);f.seq=u32(raw+8);f.length=len;
        memcpy(f.data,raw+14,len);return true;
    }
};
inline bool matches(const Frame& f,uint8_t type,uint32_t session,uint32_t seq) {
    return f.session==session && f.seq==seq && (f.type==(type|0x80) ||
        (f.type==Error && f.length==2 && f.data[0]==type));
}
struct Photo {
    uint8_t id[16]{}; uint32_t size=0,crc=0; uint64_t captured=0; char name[65]{};
    bool parse(const Frame& f) {
        if(f.type!=0x86 || f.length<35 || f.data[0]!=0) return false;
        unsigned n=f.data[33];
        if(n<1||n>64||f.length!=34+n||zeroId(f.data+1)) return false;
        const uint8_t* p=f.data+34;
        for(unsigned i=0;i<n;i++) {
            bool alnum=(p[i]>='a'&&p[i]<='z')||(p[i]>='A'&&p[i]<='Z')||(p[i]>='0'&&p[i]<='9');
            if(!alnum && (i==0 || (p[i]!='_'&&p[i]!='-'&&p[i]!='.'))) return false;
        }
        size=u32(f.data+17);crc=u32(f.data+21);captured=u64(f.data+25);
        if(size<54||size>8U*1024*1024 || (captured && (captured<946684800ULL||captured>4102444799ULL))) return false;
        memcpy(id,f.data+1,16);memcpy(name,p,n);name[n]=0;return true;
    }
    bool block(const Frame& f,uint32_t offset,uint16_t expected) const {
        return f.type==0x87 && expected>=1 && expected<=512 && offset<size && expected<=size-offset &&
            f.length==22+expected && memcmp(f.data,id,16)==0 && u32(f.data+16)==offset && u16(f.data+20)==expected;
    }
};
// A command is retained until its terminal event is accepted or definitively rejected.
// Keep the last retired ID too, so a repeated claim cannot trigger another action.
struct CommandGuard {
    uint8_t active[16]{},retired[16]{}; bool occupied=false,sent=false;
    bool claim(const uint8_t* id) {
        if(zeroId(id)||occupied||memcmp(id,retired,16)==0) return false;
        memcpy(active,id,16);occupied=true;sent=false;return true;
    }
    bool dispatchOnce() { if(!occupied||sent) return false;sent=true;return true; }
    void retire() { if(occupied) memcpy(retired,active,16); occupied=false;sent=false; }
};
} // namespace wire
