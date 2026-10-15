#include "../device/protocol.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <random>
using namespace wire;
int main() {
    assert(crc32(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926U);
    assert(crc32(nullptr,0)==0);
    uint8_t src[]={0x11,0x22,0,0x33},coded[8]{},decoded[8]{};
    uint8_t expected[]={3,0x11,0x22,2,0x33};
    assert(cobsEncode(src,4,coded,8)==5 && memcmp(coded,expected,5)==0);
    assert(cobsDecode(expected,5,decoded,8)==4 && memcmp(src,decoded,4)==0);
    assert(cobsEncode(src,4,coded,2)==0);
    uint8_t truncated[]={4,1,2},zero[]={2,0};
    assert(cobsDecode(truncated,3,decoded,8)==0 && cobsDecode(zero,2,decoded,8)==0);
    std::mt19937 rng(20261014); Frame f,r;Decoder d;uint8_t bytes[MaxWire];
    f.type=Command;f.session=0x12345678;f.seq=0x10203040;
    for(size_t n=0;n<=MaxPayload;n++) {
        f.length=n;for(size_t i=0;i<n;i++) f.data[i]=rng()%256;
        size_t len=encode(f,bytes,sizeof bytes);assert(len>=20&&len<=MaxWire);
        bool got=false;for(size_t i=0;i<len;i++) got=d.feed(bytes[i],100,r)||got;
        assert(got && r.length==n && r.session==f.session && r.seq==f.seq && memcmp(r.data,f.data,n)==0);
    }
    f.length=MaxPayload+1;assert(!encode(f,bytes,sizeof bytes));f.length=21;
    size_t len=encode(f,bytes,sizeof bytes);
    // Every single-bit corruption in this frame must be discarded.
    for(size_t pos=1;pos<len-1;pos++) for(unsigned bit=0;bit<8;bit++) {
        d.reset();bool got=false;bytes[pos]^=1U<<bit;
        for(size_t i=0;i<len;i++) got=d.feed(bytes[i],100,r)||got;
        assert(!got);bytes[pos]^=1U<<bit;
    }
    // ROM junk/oversize frame cannot poison the following valid packet.
    for(size_t i=0;i<MaxEncoded+40;i++) assert(!d.feed(0x7f,100,r));
    bool got=false;for(size_t i=0;i<len;i++) got=d.feed(bytes[i],100,r)||got;assert(got);
    d.reset();for(size_t i=0;i<len/2;i++) assert(!d.feed(bytes[i],0xffffff00,r));
    for(size_t i=len/2;i<len;i++) assert(!d.feed(bytes[i],0x100,r));
    got=false;for(size_t i=0;i<len;i++) got=d.feed(bytes[i],0x101,r)||got;assert(got);
    assert(elapsed(0x20,0xfffffff0,48));
    r.type=0x83;r.session=f.session;r.seq=f.seq;
    assert(matches(r,Command,f.session,f.seq));assert(!matches(r,Command,f.session+1,f.seq));
    assert(!matches(r,Command,f.session,f.seq+1));r.type=Error;r.length=2;r.data[0]=Status;
    assert(!matches(r,Command,f.session,f.seq));r.data[0]=Command;assert(matches(r,Command,f.session,f.seq));
    uint8_t id[16];assert(unhex("0123456789abcdef0123456789abcdef",id,16));
    assert(!unhex("0123456789ABCDEF0123456789abcdef",id,16));
    assert(!unhex("00",id,16));char text[33];hex(id,16,text);assert(strlen(text)==32);
    CommandGuard guard;memset(id,0x11,16);
    assert(guard.claim(id));assert(guard.dispatchOnce());assert(!guard.dispatchOnce());assert(!guard.claim(id));
    guard.retire();assert(!guard.claim(id));id[0]++;assert(guard.claim(id));
    Photo photo;Frame meta;meta.type=0x86;meta.length=39;meta.data[0]=0;
    memcpy(meta.data+1,id,16);put32(meta.data+17,1025);put32(meta.data+21,0x12345678);
    put64(meta.data+25,0);meta.data[33]=5;memcpy(meta.data+34,"a.bmp",5);
    assert(photo.parse(meta)&&photo.size==1025&&photo.crc==0x12345678);
    meta.data[34]='/';assert(!photo.parse(meta));meta.data[34]='a';
    put32(meta.data+17,9*1024*1024);assert(!photo.parse(meta));put32(meta.data+17,1025);
    assert(photo.parse(meta));Frame block;block.type=0x87;block.length=23;memcpy(block.data,id,16);
    put32(block.data+16,1024);put16(block.data+20,1);assert(photo.block(block,1024,1));
    assert(!photo.block(block,1023,1));assert(!photo.block(block,1025,1));
    block.data[0]^=1;assert(!photo.block(block,1024,1));
    puts("PASS: CRC/COBS, 545 payload lengths, corruption, resync, timeout wrap, identity, dedup, photo bounds");
}
