#include <assert.h>
#include <stdio.h>
#include "../FDwork/shared/wifi_transport.h"
static WifiTransport t;
static unsigned calls;
static int dispatch(unsigned type,const uint8_t *in,unsigned n,uint8_t *out,uint8_t *response) {
    (void)response;calls++;
    if(type==5) return -1;
    memcpy(out,in,n);return n;
}
static unsigned request(uint8_t *wire,unsigned type,unsigned session,unsigned seq,const uint8_t *p,unsigned n) {
    uint8_t raw[WF_RAW]={0x57,0x46,1};raw[3]=type;
    wf_put32(raw+4,session);wf_put32(raw+8,seq);wf_put16(raw+12,n);
    if(n) memcpy(raw+14,p,n);
    wf_put32(raw+14+n,wf_crc(raw,14+n));
    return wf_encode(wire,raw,n+18);
}
static void feed(const uint8_t *wire,unsigned n,uint32_t time) {
    t.tx_pos=t.tx_len;for(unsigned i=0;i<n;i++) wifi_transport_byte(&t,wire[i],time+i,dispatch);
}
int main(void) {
    uint8_t wire[WF_WIRE],copy[WF_WIRE],payload[WF_PAYLOAD];unsigned n;
    assert(wf_crc("123456789",9)==0xcbf43926u);
    for(unsigned size=0;size<=544;size++) {
        for(unsigned i=0;i<size;i++) payload[i]=(uint8_t)(i+size);
        n=request(wire,7,0x12345678,5,payload,size);assert(n<=WF_WIRE);
        memcpy(copy,wire+1,n-2);assert(wf_decode(copy,n-2)==size+18);
        assert(!memcmp(copy+14,payload,size));
        assert(wf_encode_in_place(copy,size+18)==n && !memcmp(copy,wire,n));
        copy[5]^=1;assert(!wf_decode(copy+1,n-2));
    }
    n=request(wire,2,1,1,0,0);feed(wire,n,1);assert(!calls && t.error_tx);
    n=request(wire,1,1,1,0,0);feed(wire,n,100);assert(calls==1 && t.reply_len);
    memcpy(copy,t.reply,t.reply_len);unsigned cached=t.reply_len;
    feed(wire,n,200);assert(calls==1 && !memcmp(copy,t.reply,cached));
    n=request(wire,2,1,1,0,0);feed(wire,n,300);assert(calls==1 && t.error_tx);
    n=request(wire,1,1,1,0,0);feed(wire,n,400);assert(!t.error_tx && calls==1);
    n=request(wire,3,1,2,(const uint8_t *)"abc",3);feed(wire,n,500);assert(calls==2);
    n=request(wire,1,1,1,0,0);feed(wire,n,600);assert(calls==2 && t.error_tx);
    n=request(wire,1,2,1,0,0);feed(wire,n,700);assert(calls==3);
    // Corruption, truncated frames, oversized frames and timeout resync.
    n=request(wire,2,2,2,0,0);wire[n-3]^=4;feed(wire,n,800);assert(calls==3);
    for(unsigned i=0;i<700;i++) wifi_transport_byte(&t,1,1000,dispatch);
    wifi_transport_byte(&t,0,1000,dispatch);assert(calls==3);
    n=request(wire,2,2,2,0,0);feed(wire,n/2,1100);feed(wire+n/2,n-n/2,2000);assert(calls==3);
    feed(wire,n,2100);assert(calls==4);
    // Defined requests fit 24 payload bytes; invalid oversize is discarded.
    n=request(wire,3,2,3,payload,544);feed(wire,n,2200);assert(calls==4);
    n=request(wire,5,2,3,0,0);feed(wire,n,3000);assert(t.pending && calls==5);
    t.reply[14]=0;t.reply[15]=1;wifi_reply(&t,0x85,2);assert(!t.pending);
    feed(wire,n,3100);assert(calls==5 && !t.error_tx);
    puts("PASS Wi-Fi CRC/COBS all sizes, corruption, sessions, replay/conflict, timeout, async cache");
}
