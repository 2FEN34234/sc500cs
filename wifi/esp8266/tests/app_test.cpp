// Execute the actual firmware state logic with deterministic UART/HTTP adapters.
// These tests deliberately do not claim RF, TLS, filesystem durability or hardware coverage.
#include <assert.h>
#include <iostream>
#include "../device/app.cpp"

namespace {
const char* TestId="11111111111111111111111111111111";
const char* TestLease="22222222222222222222222222222222";
unsigned actions=0,reads=0,commits=0;
uint32_t lastActionSeq=0;
uint32_t fakeBoot=123;
std::function<void(const wire::Frame&,wire::Frame&)> mcuHook;
void resetTest() {
    fakeMs=100000;online=true;beatConfirmed=true;beatDirty=false;authFault=false;statusValid=false;
    guard=wire::CommandGuard{};commandRunning=false;eventPending=false;netDelay=0;backoff=1000;
    session=77;seq=0;mcuBoot=123;caps=15;fault=None;photoPhase=Idle;photoWaitMs=0;actions=reads=commits=0;lastActionSeq=0;
    Serial.rx.clear();Serial.pos=0;WiFi.connection=WL_CONNECTED;fakeBoot=123;configured=true;
    lastLink=lastBeat=lastPoll=lastResult=lastEvent=lastPhotoPoll=lastRtc=fakeMs;rtcAttempted=true;
    randomId(bootId);decoder.reset();
    serialHook=[](const uint8_t* p,size_t n){
        wire::Decoder dec;wire::Frame req,res;bool got=false;
        for(size_t i=0;i<n;i++)got=dec.feed(p[i],fakeMs,req)||got;
        assert(got);res.type=req.type|0x80;res.session=req.session;res.seq=req.seq;
        if(req.type==wire::Hello){res.length=8;wire::put32(res.data,fakeBoot);wire::put32(res.data+4,15);}
        else if(req.type==wire::Command){
            if(lastActionSeq!=req.seq){actions++;lastActionSeq=req.seq;}
            res.length=17;memcpy(res.data,req.data,16);res.data[16]=0;
        }
        else if(req.type==wire::Result){res.length=35;memcpy(res.data,req.data,16);res.data[16]=1;memset(res.data+19,0x33,16);}
        if(mcuHook)mcuHook(req,res);
        if(res.type==0)return;
        uint8_t packet[wire::MaxWire];size_t count=wire::encode(res,packet,sizeof packet);assert(count);Serial.supply(packet,count);
    };
    mcuHook=nullptr;
    httpHook=[](const std::string&,const std::string&,const std::string&){return HttpResult{204,""};};
}
HttpResult command(const char* kind="capture",const char* args="{}") {
    return {200,std::string("{\"command_id\":\"")+TestId+"\",\"lease_id\":\""+TestLease+
        "\",\"lease_s\":120,\"expires_at\":4102444799,\"kind\":\""+kind+"\",\"args\":"+args+"}"};
}
HttpResult eventReply(const std::string& body) {
    StaticJsonDocument<1024> event;assert(!deserializeJson(event,body));
    return {200,std::string("{\"accepted\":true,\"command_id\":\"")+TestId+"\",\"state\":\""+
        event["state"].as<const char*>()+"\"}"};
}
void preparePhoto() {
    memset(photo.id,0x33,16);photo.size=1025;photo.crc=0x12345678;photo.captured=0;strcpy(photo.name,"TEST.bmp");
    photoOffset=0;photoPhase=Register;
    mcuHook=[](const wire::Frame& req,wire::Frame& res){
        if(req.type==wire::PhotoRead) {
            reads++;uint16_t n=wire::u16(req.data+20);res.length=22+n;memcpy(res.data,req.data,22);
            memset(res.data+22,0x55,n);
        } else if(req.type==wire::PhotoCommit) {
            commits++;res.length=17;memcpy(res.data,req.data,16);res.data[16]=0;
        }
    };
}
HttpResult photoReply(unsigned offset,bool complete=false) {
    return {200,std::string("{\"photo_id\":\"33333333333333333333333333333333\",\"size\":1025,\"crc32\":\"12345678\",\"next_offset\":")+
        std::to_string(offset)+",\"complete\":"+(complete?"true":"false")+"}"};
}
}
int main() {
    resetTest();httpHook=[](const std::string&,const std::string&,const std::string&){return command();};
    claimCommand();assert(actions==1 && guard.occupied && commandRunning && !strcmp(eventState,"dispatched"));
    // ACK is not completion; the event only becomes succeeded after the MCU RESULT.
    httpHook=[](const std::string&,const std::string&,const std::string& b){return eventReply(b);};
    sendEvent();assert(!eventPending&&guard.occupied);pollResult();assert(!strcmp(eventState,"succeeded"));
    sendEvent();assert(!guard.occupied);
    httpHook=[](const std::string&,const std::string&,const std::string&){return command();};
    claimCommand();assert(actions==1&&!guard.occupied);

    resetTest();mcuHook=[](const wire::Frame& req,wire::Frame& res){if(req.type==wire::Command)res.type=0;};
    httpHook=[](const std::string&,const std::string&,const std::string&){return command();};
    claimCommand();assert(actions==1&&!online&&!strcmp(eventState,"unknown"));
    assert(guard.sent&&guard.occupied); // Four retries, same sequence, one action.
    std::string frozenId=eventId;
    httpHook=[](const std::string&,const std::string&,const std::string&){return HttpResult{-1,""};};
    sendEvent();assert(eventPending&&frozenId==eventId);
    netDelay=0;httpHook=[](const std::string&,const std::string&,const std::string&){return HttpResult{409,"{\"error\":{\"code\":\"TERMINAL_STATE\"}}"};};
    sendEvent();assert(!guard.occupied&&actions==1);

    resetTest();httpHook=[](const std::string&,const std::string&,const std::string&){return command("set_parameters","{\"exposure\":6000,\"gain\":16}");};
    claimCommand();assert(actions==0&&!strcmp(eventState,"failed"));
    resetTest();httpHook=[](const std::string&,const std::string&,const std::string&){return command();};
    claimCommand();eventPending=false;fakeBoot++;
    assert(hello());assert(!commandRunning&&!strcmp(eventState,"unknown"));

    resetTest();online=false;
    httpHook=[](const std::string&,const std::string&,const std::string& b){
        StaticJsonDocument<1024> beat;assert(!deserializeJson(beat,b));assert(!beat["mcu_online"].as<bool>()&&beat["mcu"].isNull());
        return HttpResult{200,"{\"protocol\":1,\"server_time\":1791972000,\"heartbeat_interval_s\":30}"};
    };
    heartbeat();assert(beatConfirmed&&!beatDirty);
    resetTest();httpHook=[](const std::string&,const std::string&,const std::string&){return HttpResult{401,"denied","text/plain"};};
    api("GET","/time");assert(authFault&&!netReady());
    resetTest();httpHook=[](const std::string&,const std::string&,const std::string&){return HttpResult{200,std::string(2049,'x')};};
    assert(api("GET","/time")<0&&netDelay>0);

    resetTest();preparePhoto();httpHook=[](const std::string&,const std::string&,const std::string&){return photoReply(0);};
    photoStep();assert(photoPhase==Read&&commits==0);
    // HTTP failure after a possible durable write -> status query, not local offset advancement.
    httpHook=[](const std::string&,const std::string&,const std::string& b){assert(b.size()==512);return HttpResult{-1,""};};
    photoStep();assert(photoPhase==Query&&photoOffset==0&&commits==0);
    netDelay=0;httpHook=[](const std::string& method,const std::string&,const std::string&){assert(method=="GET");return photoReply(512);};
    photoStep();assert(photoOffset==512&&photoPhase==Read);
    httpHook=[](const std::string&,const std::string&,const std::string& b){assert(b.size()==512);return photoReply(1024);};
    photoStep();assert(photoOffset==1024&&commits==0);
    httpHook=[](const std::string&,const std::string&,const std::string& b){assert(b.size()==1);return photoReply(1025);};
    photoStep();assert(photoPhase==Complete&&commits==0);
    httpHook=[](const std::string&,const std::string&,const std::string&){return photoReply(1025,true);};
    photoStep();assert(photoPhase==Commit&&commits==0);photoStep();assert(commits==1&&photoPhase==Idle);

    resetTest();preparePhoto();photoPhase=Complete;
    httpHook=[](const std::string&,const std::string&,const std::string&){return HttpResult{409,"{\"error\":{\"code\":\"CRC_MISMATCH\"}}"};};
    photoStep();assert(photoPhase==Paused&&commits==0);
    resetTest();preparePhoto();photoPhase=Query;
    httpHook=[](const std::string&,const std::string&,const std::string&){return photoReply(1025,true);};
    photoStep();photoStep();assert(commits==1); // Lost MCU commit ACK recovered from server's completed photo.
    resetTest();preparePhoto();photoPhase=Commit;
    mcuHook=[](const wire::Frame& req,wire::Frame& res){
        if(req.type==wire::PhotoCommit){commits++;res.length=17;memcpy(res.data,req.data,16);res.data[16]=commits==1?1:0;}
    };
    photoStep();assert(photoPhase==Commit&&commits==1);photoStep();assert(commits==1);
    fakeMs+=1000;photoStep();assert(photoPhase==Idle&&commits==2);
    resetTest();preparePhoto();photoPhase=Complete;
    httpHook=[](const std::string&,const std::string&,const std::string&){
        auto response=photoReply(1025,true);response.body.replace(response.body.find("12345678"),8,"87654321");return response;
    };
    photoStep();assert(photoPhase==Query&&commits==0);
    resetTest();
    httpHook=[](const std::string&,const std::string&,const std::string&){return HttpResult{200,"{\"unix_s\":1791972000,\"rtc_utc_offset_min\":480}"};};
    mcuHook=[](const wire::Frame& req,wire::Frame& res){if(req.type==wire::Time){res.length=2;res.data[0]=9;res.data[1]=0;}};
    syncRtc();assert(!rtcVerified&&fault==Rtc);
    std::cout<<"PASS: actual firmware command states/retries, reboot, offline heartbeat, auth/size rejection, resumable upload and commit gating\n";
}
