/* Compile the actual ESP chat's protocol header read-only against our MCU
 * codec. No Arduino source modifications and no network/device access. */
#include <cassert>
#include <cstdio>
#include "../wifi/esp8266/device/protocol.h"
#include "../FDwork/shared/wifi_transport.h"
static int dispatch(unsigned type,const uint8_t *in,unsigned n,uint8_t *out,uint8_t *response) {
    (void)response;
    if(type==1) {wf_put32(out,123);wf_put32(out+4,15);return 8;}
    if(type==7) {assert(n==22);memcpy(out,in,22);for(unsigned i=0;i<wf_get16(in+20);i++) out[22+i]=uint8_t(i);return 22+wf_get16(in+20);}
    return 0;
}
int main() {
    WifiTransport mcu{};wire::Frame request,reply;wire::Decoder esp;
    uint8_t packet[wire::MaxWire];request.type=1;request.session=0x12345678;request.seq=1;
    size_t n=wire::encode(request,packet,sizeof packet);
    for(size_t i=0;i<n;i++) wifi_transport_byte(&mcu,packet[i],i,dispatch);
    bool ok=false;
    for(unsigned i=0;i<mcu.reply_len;i++) ok|=esp.feed(mcu.reply[i],i,reply);
    assert(ok && wire::matches(reply,1,request.session,1) && wire::u32(reply.data)==123);
    request.type=7;request.seq=2;request.length=22;memset(request.data,0xab,16);
    wire::put32(request.data+16,512);wire::put16(request.data+20,512);
    n=wire::encode(request,packet,sizeof packet);
    for(size_t i=0;i<n;i++) wifi_transport_byte(&mcu,packet[i],100+i,dispatch);
    ok=false;for(unsigned i=0;i<mcu.reply_len;i++) ok|=esp.feed(mcu.reply[i],200+i/10,reply);
    assert(ok && wire::matches(reply,7,request.session,2));
    wire::Photo photo;memcpy(photo.id,request.data,16);photo.size=691254;
    assert(photo.block(reply,512,512));
    for(unsigned i=0;i<512;i++) assert(reply.data[22+i]==uint8_t(i));
    // Identical retry returns the same block without re-reading the SD card.
    uint8_t saved[wire::MaxWire];memcpy(saved,mcu.reply,mcu.reply_len);unsigned saved_len=mcu.reply_len;
    for(size_t i=0;i<n;i++) wifi_transport_byte(&mcu,packet[i],500+i,dispatch);
    assert(saved_len==mcu.reply_len && !memcmp(saved,mcu.reply,saved_len));
    // Both encoders must agree for every possible payload length.
    for(unsigned len=0;len<=544;len++) {
        wire::Frame f;f.type=0x87;f.session=123;f.seq=456;f.length=len;
        for(unsigned i=0;i<len;i++) f.data[i]=uint8_t(i*37+len);
        n=wire::encode(f,packet,sizeof packet);
        uint8_t raw[WF_WIRE];raw[0]=0x57;raw[1]=0x46;raw[2]=1;raw[3]=f.type;
        wf_put32(raw+4,f.session);wf_put32(raw+8,f.seq);wf_put16(raw+12,len);
        memcpy(raw+14,f.data,len);wf_put32(raw+14+len,wf_crc(raw,14+len));
        assert(wf_encode_in_place(raw,len+18)==n && !memcmp(raw,packet,n));
    }
    std::puts("PASS actual Arduino/MCU v1 codecs, HELLO, 512-byte PHOTO_DATA, retry, all lengths");
}
