#ifndef WIFI_WIRE_H
#define WIFI_WIRE_H
#include <stdint.h>
#include <string.h>

#define WF_PAYLOAD 544u
#define WF_RAW 562u
#define WF_WIRE 567u
enum { WF_OK, WF_BUSY, WF_BAD_ARGUMENT, WF_UNSUPPORTED, WF_IO_ERROR,
       WF_UNKNOWN_ID, WF_NO_PHOTO, WF_CONFLICT, WF_IN_PROGRESS, WF_RTC_VERIFY_FAILED };
static inline uint16_t wf_get16(const uint8_t *p) {return p[0]|((uint16_t)p[1]<<8);}
static inline uint32_t wf_get32(const uint8_t *p) {return wf_get16(p)|((uint32_t)wf_get16(p+2)<<16);}
static inline void wf_put16(uint8_t *p,uint16_t v) {p[0]=v;p[1]=v>>8;}
static inline void wf_put32(uint8_t *p,uint32_t v) {wf_put16(p,v);wf_put16(p+2,v>>16);}
static inline uint32_t wf_crc_update(uint32_t c,const void *data,unsigned n) {
    const uint8_t *p=(const uint8_t *)data;
    while(n--) {c^=*p++;for(unsigned b=0;b<8;b++) c=(c>>1)^((0u-(c&1))&0xedb88320u);}
    return c;
}
static inline uint32_t wf_crc(const void *p,unsigned n) {return ~wf_crc_update(0xffffffffu,p,n);}
/* Input raw and output wire must not overlap. */
static inline unsigned wf_encode(uint8_t *out,const uint8_t *raw,unsigned n) {
    unsigned code_at=1,w=2;uint8_t code=1;out[0]=0;
    for(unsigned i=0;i<n;i++) {
        if(raw[i]) {out[w++]=raw[i];if(++code!=255) continue;}
        out[code_at]=code;code_at=w++;code=1;
    }
    out[code_at]=code;out[w++]=0;return w;
}
/* Three COBS code bytes suffice for the maximum 562-byte raw frame. Move
 * source four bytes ahead so forward encoding can safely share one buffer. */
static inline unsigned wf_encode_in_place(uint8_t *p,unsigned n) {
    memmove(p+4,p,n);
    unsigned code_at=1,w=2;uint8_t code=1;p[0]=0;
    for(unsigned i=0;i<n;i++) {
        uint8_t b=p[4+i];
        if(b) {p[w++]=b;if(++code!=255) continue;}
        p[code_at]=code;code_at=w++;code=1;
    }
    p[code_at]=code;p[w++]=0;return w;
}
/* Decode in place. Encoded input excludes zero delimiters. */
static inline unsigned wf_decode(uint8_t *p,unsigned n) {
    unsigned r=0,w=0;
    while(r<n) {
        unsigned c=p[r++];if(!c || r+c-1>n) return 0;
        for(unsigned i=1;i<c;i++) {if(!p[r]) return 0;p[w++]=p[r++];}
        if(c!=255 && r<n) p[w++]=0;
    }
    if(w<18 || p[0]!=0x57 || p[1]!=0x46 || p[2]!=1 ||
       wf_get16(p+12)>WF_PAYLOAD || w!=18u+wf_get16(p+12) ||
       wf_crc(p,w-4)!=wf_get32(p+w-4)) return 0;
    return w;
}
#endif
