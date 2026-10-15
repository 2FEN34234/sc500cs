#include <iostream>
#include <assert.h>
#include "../device/app.cpp"
extern "C" {
void integration_reset(void);
unsigned integration_exchange(const uint8_t*,unsigned,uint32_t,uint8_t*);
}
std::vector<uint8_t> bmp;
uint8_t activeCommand[16],imageId[16];
unsigned actionCount=0,commitCount=0;
bool photoAvailable=false,photoMarked=false;
extern "C" int integration_dispatch(unsigned type,const uint8_t* in,unsigned n,uint8_t* out,uint8_t* response) {
    using namespace wire;
    switch(type) {
    case Hello:put32(out,4321);put32(out+4,15);return 8;
    case Status:put16(out,100);put16(out+2,32);put32(out+4,5);put16(out+8,6);put16(out+10,127);return 12;
    case Command:
        assert(n==17&&in[16]==1);actionCount++;memcpy(activeCommand,in,16);
        photoAvailable=true;memcpy(out,in,16);out[16]=0;return 17;
    case Result:
        assert(n==16&&memcmp(in,activeCommand,16)==0);memcpy(out,in,16);out[16]=1;put16(out+17,0);
        memcpy(out+19,imageId,16);return 35;
    case Time:out[0]=0;out[1]=1;return 2;
    case PhotoNext:
        if(!photoAvailable||photoMarked){out[0]=6;return 1;}
        out[0]=0;memcpy(out+1,imageId,16);put32(out+17,bmp.size());put32(out+21,crc32(bmp.data(),bmp.size()));
        put64(out+25,0);out[33]=12;memcpy(out+34,"SIM_TEST.BMP",12);return 46;
    case PhotoRead: {
        assert(n==22&&memcmp(in,imageId,16)==0);uint32_t offset=u32(in+16);unsigned count=u16(in+20);
        assert(offset+count<=bmp.size());memcpy(out,in,22);memcpy(out+22,bmp.data()+offset,count);return 22+count;
    }
    case PhotoCommit:
        assert(n==24&&memcmp(in,imageId,16)==0&&u32(in+16)==bmp.size()&&u32(in+20)==crc32(bmp.data(),bmp.size()));
        commitCount++;photoMarked=true;memcpy(out,in,16);out[16]=0;return 17;
    default:*response=255;out[0]=type;out[1]=3;return 2;
    }
}
int main() {
    using namespace wire;
    bmp.assign(54+32*16*3,0x55);memset(bmp.data(),0,54);bmp[0]='B';bmp[1]='M';put32(bmp.data()+2,bmp.size());
    put32(bmp.data()+10,54);put32(bmp.data()+14,40);put32(bmp.data()+18,32);put32(bmp.data()+22,16);
    put16(bmp.data()+26,1);put16(bmp.data()+28,24);put32(bmp.data()+34,bmp.size()-54);memset(imageId,0x33,16);
    integration_reset();configured=true;session=777;randomId(bootId);fakeMs=100000;
    serialHook=[](const uint8_t* p,size_t n){uint8_t response[MaxWire];unsigned count=integration_exchange(p,n,fakeMs,response);Serial.supply(response,count);};
    httpHook=[](const std::string& method,const std::string& url,const std::string& payload){
        StaticJsonDocument<6144> message;message["method"]=method;message["url"]=url;
        std::string encoded(payload.size()*2,'0');
        if(!payload.empty()){std::vector<char> tmp(payload.size()*2+1);hex(reinterpret_cast<const uint8_t*>(payload.data()),payload.size(),tmp.data());encoded=tmp.data();}
        message["hex"]=encoded;std::string line;serializeJson(message,line);std::cout<<line<<std::endl;
        if(!std::getline(std::cin,line)) std::exit(2);
        StaticJsonDocument<4096> response;if(deserializeJson(response,line)) std::exit(3);
        return HttpResult{response["code"].as<int>(),response["body"].as<std::string>()};
    };
    for(unsigned i=0;i<600&&!photoMarked;i++){fakeMs+=1500;cameraLoop();}
    assert(actionCount==1&&commitCount==1&&bmp.size()==1590); // Original simulated SD data retained.
    std::cout<<"{\"done\":true,\"actions\":"<<actionCount<<",\"commits\":"<<commitCount<<"}"<<std::endl;
}
