#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>
extern "C" {
#include <user_interface.h>
}
#include "protocol.h"
#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif
#if __has_include("ca.local.h")
#include "ca.local.h"
#else
#include "ca.example.h"
#endif

namespace {
using namespace wire;
constexpr uint32_t StatusPeriod=5000, HeartbeatPeriod=30000;
constexpr uint64_t MinTime=1767225600ULL, MaxTime=4102444799ULL; // 2026..2099 UTC
enum Fault : uint16_t { None=0, Config=1, Auth=2, Http=3, Uart=4, PhotoFailure=5, Rtc=6 };
// Inspect with a debugger if needed; no ASCII diagnostics on the MCU UART.
volatile Fault fault=None;
volatile bool configured=false;
bool online=false,beatConfirmed=false,beatDirty=true,authFault=false;
bool statusValid=false,rtcVerified=false;
uint32_t session=0,seq=0,mcuBoot=0,caps=0,lastLink=0,lastStatus=0,lastBeat=0,lastPoll=0;
uint32_t lastWifi=0,lastRtc=0,netFailedAt=0,netDelay=0,backoff=1000,lastPhotoPoll=0;
bool rtcAttempted=false;
char bootId[33],commandId[33],leaseId[33],eventId[33],eventState[16],eventCode[48],eventPhoto[33];
bool eventPending=false,commandRunning=false;
uint8_t commandOpcode=0;
uint32_t lastResult=0,lastEvent=0,leaseAt=0,leaseMs=0;
CommandGuard guard;
Frame reply,requestFrame;
Decoder decoder;
uint8_t packet[MaxWire],statusBytes[12];
StaticJsonDocument<2304> doc;
char body[2049];
BearSSL::WiFiClientSecure tls;
BearSSL::Session tlsSession;
BearSSL::X509List* trust=nullptr;
HTTPClient http;
Photo photo;
enum PhotoPhase { Idle,Register,Query,Read,Complete,Commit,Paused };
PhotoPhase photoPhase=Idle;
uint32_t photoOffset=0;
uint32_t photoWaitAt=0,photoWaitMs=0;

void randomId(char* out) { uint8_t b[16]; os_get_random(b,sizeof b); hex(b,sizeof b,out); }
uint32_t randomNonzero() { uint32_t v=0; while(!v) os_get_random(reinterpret_cast<uint8_t*>(&v),sizeof v);return v; }
bool validEpoch(uint64_t t) { return t>=MinTime && t<=MaxTime; }
bool netReady() { return !authFault && WiFi.status()==WL_CONNECTED && validEpoch(uint64_t(time(nullptr))) &&
    (!netDelay||elapsed(millis(),netFailedAt,netDelay)); }
void postpone(uint32_t delayMs=0) {
    netFailedAt=millis();netDelay=delayMs?delayMs:backoff+(randomNonzero()%250);
    if(backoff<30000) backoff=(backoff*2>30000)?30000:backoff*2;
}
void newEvent(const char* state,const char* code,const uint8_t* photoId=nullptr) {
    randomId(eventId);strlcpy(eventState,state,sizeof eventState);strlcpy(eventCode,code,sizeof eventCode);
    eventPhoto[0]=0;if(photoId) hex(photoId,16,eventPhoto);
    eventPending=true;
}
void loseLink() {
    online=false;statusValid=false;beatDirty=true;beatConfirmed=false;fault=Uart;
    if(guard.occupied && commandRunning) { commandRunning=false;newEvent("unknown","MCU_LINK_LOST"); }
    // An already-observed terminal result is retained even if the MCU later disappears.
}
bool rpc(uint8_t type,const uint8_t* data=nullptr,size_t n=0,uint32_t timeoutMs=1000) {
    if(n>MaxPayload || seq==UINT32_MAX) { loseLink();session=randomNonzero();seq=0;return false; }
    requestFrame.type=type;requestFrame.session=session;requestFrame.seq=++seq;
    requestFrame.length=n;if(n) memcpy(requestFrame.data,data,n);
    size_t len=encode(requestFrame,packet,sizeof packet);if(!len) return false;
    decoder.reset();
    // Four sends total: first transmission plus three retries of the identical sequence.
    for(unsigned attempt=0;attempt<4;attempt++) {
        Serial.write(packet,len);
        uint32_t start=millis();
        while(!elapsed(millis(),start,timeoutMs)) {
            while(Serial.available()) {
                if(decoder.feed(uint8_t(Serial.read()),millis(),reply) && matches(reply,type,session,seq)) return true;
            }
            delay(0);
        }
    }
    loseLink();return false;
}
bool hello() {
    if(!rpc(Hello)||reply.type!=0x81||reply.length!=8||!wire::u32(reply.data)||(wire::u32(reply.data+4)&~15U)) {
        loseLink();return false;
    }
    uint32_t nextBoot=wire::u32(reply.data);
    if(mcuBoot && nextBoot!=mcuBoot) {
        statusValid=false;rtcAttempted=false;
        if(guard.occupied && commandRunning) { commandRunning=false;newEvent("unknown","MCU_REBOOT"); }
        if(photoPhase!=Paused) photoPhase=Idle;
    }
    mcuBoot=nextBoot;caps=wire::u32(reply.data+4);
    if(!online) { beatDirty=true;beatConfirmed=false; }
    online=true;return true;
}
bool readStatus() {
    if(!(caps&1)) { statusValid=false;return true; }
    if(!rpc(Status)||reply.type!=0x82||reply.length!=12||(wire::u16(reply.data+8)&~15U)||(wire::u16(reply.data+10)&~127U)) {
        loseLink();return false;
    }
    memcpy(statusBytes,reply.data,12);statusValid=true;lastStatus=millis();return true;
}
String base() { return String("/api/v1/devices/")+CAMERA_DEVICE_ID; }
String photoPath() { char id[33];hex(photo.id,16,id);return String("/photos/")+id; }
bool isString(JsonVariantConst v,const char* s) { const char* p=v.as<const char*>();return p && strcmp(p,s)==0; }
bool getU32(JsonVariantConst v,uint32_t& out) { if(!v.is<uint32_t>()) return false;out=v.as<uint32_t>();return true; }
bool getId(JsonVariantConst v,char* out) {
    const char* s=v.as<const char*>();uint8_t b[16];
    if(!unhex(s,b,16)||zeroId(b)) return false;
    memcpy(out,s,33);return true;
}
// Serial RPCs complete before entering HTTPS. Only one bounded JSON body is held.
// payload!=nullptr sends binary; otherwise POST/PUT serialize the current doc.
int api(const char* method,const String& suffix,const uint8_t* payload=nullptr,size_t length=0) {
    if(!netReady()) return -1001;
    if(!payload && strcmp(method,"GET")!=0) {
        if(doc.overflowed()||measureJson(doc)>2048) { fault=Http;return -1002; }
        length=serializeJson(doc,body,sizeof body);payload=reinterpret_cast<const uint8_t*>(body);
    }
    const bool binary=payload && payload!=reinterpret_cast<const uint8_t*>(body);
    const char* headers[]={"Content-Type","Transfer-Encoding","Content-Encoding","Retry-After"};
    http.setTimeout(5000);http.setReuse(true);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if(!http.begin(tls,String("https://")+CAMERA_SERVER_HOST+":"+CAMERA_SERVER_PORT+base()+suffix)) { postpone();return -1003; }
    http.collectHeaders(headers,4);
    http.addHeader("Authorization",String("Bearer ")+CAMERA_DEVICE_TOKEN);
    http.addHeader("Accept","application/json");http.addHeader("Accept-Encoding","identity");
    if(strcmp(method,"GET")) http.addHeader("Content-Type",binary?"application/octet-stream":"application/json");
    int code=http.sendRequest(method,const_cast<uint8_t*>(payload),length);
    doc.clear();
    bool valid=code>0;
    if(code>0 && code!=204) {
        int n=http.getSize();
        valid=n>0&&n<=2048 && http.header("Content-Type").startsWith("application/json") &&
            http.header("Transfer-Encoding").length()==0 &&
            (http.header("Content-Encoding").length()==0||http.header("Content-Encoding")=="identity");
        if(valid) {
            WiFiClient* stream=http.getStreamPtr();size_t used=0;uint32_t start=millis();
            while(used<size_t(n)&&!elapsed(millis(),start,5000)) {
                int available=stream->available();
                if(available>0) {
                    size_t count=size_t(available);if(count>size_t(n)-used) count=size_t(n)-used;
                    int got=stream->read(reinterpret_cast<uint8_t*>(body)+used,count);
                    if(got>0) used+=size_t(got);
                } else if(!stream->connected()) break;
                delay(0);
            }
            body[used]=0;
            valid=used==size_t(n)&&!deserializeJson(doc,body,used,DeserializationOption::NestingLimit(6))&&doc.is<JsonObject>();
        }
    }
    if(code==401||code==403) { authFault=true;fault=Auth;tls.stop(); }
    else if(!valid) { tls.stop();fault=Http;postpone(); }
    else if(code==429) {
        long seconds=http.header("Retry-After").toInt();
        postpone(uint32_t(seconds<1?1:seconds>300?300:seconds)*1000);
    } else if(code>=500) { postpone();fault=Http; }
    else { netDelay=0;backoff=1000; }
    http.end();
    return valid?code:(code==401||code==403?code:-1004);
}
void heartbeat() {
    doc.clear();doc["protocol"]=1;doc["boot_id"]=bootId;doc["firmware"]=CAMERA_FIRMWARE_VERSION;
    doc["uptime_s"]=millis()/1000;doc["wifi_rssi_dbm"]=WiFi.RSSI();doc["free_heap_bytes"]=ESP.getFreeHeap();
    doc["mcu_online"]=online;
    if(online && statusValid) {
        JsonObject s=doc.createNestedObject("mcu");uint16_t mask=wire::u16(statusBytes+10),flags=wire::u16(statusBytes+8);
        s["exposure"]=nullptr;s["gain"]=nullptr;s["interval_s"]=nullptr;
        s["recording"]=nullptr;s["rtc_valid"]=nullptr;s["sd_ready"]=nullptr;s["busy"]=nullptr;
        if(mask&1) s["exposure"]=wire::u16(statusBytes);
        if(mask&2) s["gain"]=wire::u16(statusBytes+2);
        if(mask&4) s["interval_s"]=wire::u32(statusBytes+4);
        const char* names[]={"recording","rtc_valid","sd_ready","busy"};
        for(unsigned i=0;i<4;i++) if(mask&(8U<<i)) s[names[i]]=bool(flags&(1U<<i));
        s["age_ms"]=uint32_t(millis()-lastStatus);
    } else doc["mcu"]=nullptr;
    int code=api("POST","/heartbeat");uint32_t version,period;
    if(code==200 && getU32(doc["protocol"],version)&&version==1 &&
        doc["server_time"].is<uint64_t>()&&validEpoch(doc["server_time"].as<uint64_t>()) &&
        getU32(doc["heartbeat_interval_s"],period)&&period==30) {
        beatConfirmed=true;beatDirty=false;lastBeat=millis();
    } else { beatConfirmed=false;postpone(); }
}
void retireCommand() { guard.retire();commandRunning=false;eventPending=false; }
void sendEvent() {
    doc.clear();doc["event_id"]=eventId;doc["lease_id"]=leaseId;doc["state"]=eventState;doc["code"]=eventCode;
    JsonObject result=doc.createNestedObject("result");if(eventPhoto[0]) result["photo_id"]=eventPhoto;
    int code=api("POST",String("/commands/")+commandId+"/events");
    if(code==200 && doc["accepted"].is<bool>()&&doc["accepted"].as<bool>()&&
        isString(doc["command_id"],commandId)&&isString(doc["state"],eventState)) {
        eventPending=false;lastEvent=millis();
        if(strcmp(eventState,"dispatched")!=0) retireCommand();
        else { leaseAt=millis();leaseMs=120000; }
    } else if(code==409||code==404) {
        // Server is authoritative for a lease that expired to unknown. Do not re-execute.
        retireCommand();fault=Http;
    } else if(code>0 && code<500 && code!=429 && code!=401 && code!=403) {
        // Keep the command occupied; a malformed contract must not cause another action.
        fault=Http;postpone(30000);
    }
}
void pollResult() {
    lastResult=millis();
    if(!rpc(Result,guard.active,16)) return;
    if(reply.type!=0x84||reply.length!=35||memcmp(reply.data,guard.active,16)!=0||reply.data[16]>3) {
        commandRunning=false;newEvent("unknown","BAD_MCU_RESULT");return;
    }
    uint8_t state=reply.data[16];uint16_t code=wire::u16(reply.data+17);
    if(state==0) return;
    commandRunning=false;
    if(state==1 && (code!=0 || (commandOpcode==1 && zeroId(reply.data+19)))) {
        newEvent("unknown","BAD_MCU_RESULT");return;
    }
    char reason[48];snprintf(reason,sizeof reason,"MCU_%u",unsigned(code));
    newEvent(state==1?"succeeded":state==2?"failed":"unknown",state==1?"OK":reason,
        state==1&&commandOpcode==1?reply.data+19:nullptr);
}
void claimCommand() {
    lastPoll=millis();if(!hello()||!online||!(caps&2)||beatDirty||!beatConfirmed) return;
    int code=api("GET","/commands/next");lastPoll=millis();if(code==204) return;
    if(code!=200) return;
    char nextId[33],nextLease[33];uint32_t seconds;
    if(!getId(doc["command_id"],nextId)||!getId(doc["lease_id"],nextLease)||
        !getU32(doc["lease_s"],seconds)||seconds<1||seconds>120||!doc["expires_at"].is<uint64_t>()||
        doc["expires_at"].as<uint64_t>()<=uint64_t(time(nullptr))) { fault=Http;postpone();return; }
    uint8_t id[16];unhex(nextId,id,16);
    if(!guard.claim(id)) return;
    memcpy(commandId,nextId,33);memcpy(leaseId,nextLease,33);leaseAt=millis();leaseMs=seconds*1000;
    JsonObjectConst args=doc["args"].as<JsonObjectConst>();const char* kind=doc["kind"].as<const char*>();
    uint8_t payload[21];memcpy(payload,id,16);size_t size=17;uint32_t a=0,b=0;
    bool valid=kind && !args.isNull();commandOpcode=0;
    if(valid && !strcmp(kind,"capture")) { commandOpcode=1;valid=args.size()==0; }
    else if(valid && !strcmp(kind,"set_parameters")) {
        commandOpcode=2;valid=args.size()==2&&getU32(args["exposure"],a)&&getU32(args["gain"],b)&&a>=3&&a<=5990&&b>=16&&b<=143;
        put16(payload+17,a);put16(payload+19,b);size=21;
    } else if(valid && !strcmp(kind,"timelapse_start")) {
        commandOpcode=3;valid=args.size()==1&&getU32(args["interval_s"],a)&&interval(a);put32(payload+17,a);size=21;
    } else if(valid && !strcmp(kind,"timelapse_stop")) { commandOpcode=4;valid=args.size()==0; }
    else valid=false;
    payload[16]=commandOpcode;
    if(!valid) { newEvent("failed","BAD_ARGUMENT");return; }
    // Mark possibly executed before transmitting any bytes. Never reissue a fresh COMMAND.
    if(!guard.dispatchOnce()) return;
    commandRunning=true;
    if(!rpc(Command,payload,size)) return;
    if(reply.type==0x83 && reply.length==17 && memcmp(reply.data,id,16)==0) {
        if(reply.data[16]==0||reply.data[16]==8) { lastResult=millis();newEvent("dispatched","OK"); }
        else { commandRunning=false;newEvent("failed","MCU_REJECTED"); }
    } else { commandRunning=false;newEvent("unknown","BAD_MCU_ACK"); }
}
void syncRtc() {
    lastRtc=millis();rtcAttempted=true;
    if(api("GET","/time")!=200||!doc["unix_s"].is<uint64_t>()||!validEpoch(doc["unix_s"].as<uint64_t>())||
        !doc["rtc_utc_offset_min"].is<int>()||doc["rtc_utc_offset_min"].as<int>()!=480) { fault=Rtc;return; }
    uint64_t epoch=doc["unix_s"].as<uint64_t>();
    timeval tv{};tv.tv_sec=static_cast<decltype(tv.tv_sec)>(epoch);settimeofday(&tv,nullptr);
    uint8_t p[10];put64(p,epoch);put16(p+8,480);
    rtcVerified=rpc(Time,p,sizeof p,5000)&&reply.type==0x85&&reply.length==2&&reply.data[0]==0&&reply.data[1]==1;
    if(!rtcVerified) fault=Rtc;
}
void photoError() { photoPhase=Paused;fault=PhotoFailure; }
bool photoIdentity(bool requireSize) {
    char id[33],crc[9];hex(photo.id,16,id);snprintf(crc,sizeof crc,"%08lx",static_cast<unsigned long>(photo.crc));
    uint32_t size;
    return isString(doc["photo_id"],id) && (!requireSize ||
        (getU32(doc["size"],size)&&size==photo.size&&isString(doc["crc32"],crc)));
}
void photoStep() {
    if(photoPhase==Paused) return;
    if(photoWaitMs && !elapsed(millis(),photoWaitAt,photoWaitMs)) return;
    photoWaitMs=0;
    if(photoPhase==Idle) {
        if(!elapsed(millis(),lastPhotoPoll,5000)) return;
        lastPhotoPoll=millis();
        if(!rpc(PhotoNext)) return;
        if(reply.type==0x86 && reply.length==1 && (reply.data[0]==6||reply.data[0]==1)) return;
        if(reply.type==Error && reply.length==2 && reply.data[1]==1) return;
        if(!photo.parse(reply)) { photoError();return; }
        photoPhase=Register;photoOffset=0;return;
    }
    if(photoPhase==Commit) {
        uint8_t p[24];memcpy(p,photo.id,16);put32(p+16,photo.size);put32(p+20,photo.crc);
        if(!rpc(PhotoCommit,p,sizeof p)) { photoPhase=Idle;return; }
        if(reply.type==0x88&&reply.length==17&&memcmp(reply.data,photo.id,16)==0) {
            if(reply.data[16]==0) photoPhase=Idle;
            else if(reply.data[16]==1) { photoWaitAt=millis();photoWaitMs=1000; }
            else photoError();
        } else photoError();
        return;
    }
    if(photoPhase==Register || photoPhase==Query) {
        const bool registering=photoPhase==Register;
        if(registering) {
            doc.clear();char id[33],crc[9];hex(photo.id,16,id);snprintf(crc,sizeof crc,"%08lx",static_cast<unsigned long>(photo.crc));
            doc["photo_id"]=id;doc["name"]=photo.name;doc["size"]=photo.size;doc["crc32"]=crc;
            doc["content_type"]="image/bmp";
            if(photo.captured) doc["captured_at"]=photo.captured;else doc["captured_at"]=nullptr;
            // Serialize while id/crc are still in scope (ArduinoJson references char arrays).
            int code=api("POST","/photos");
            if(code!=200&&code!=201) { if(code>=400&&code<600&&code!=429&&code!=500&&code!=502&&code!=503&&code!=504) photoError();return; }
        } else {
            int code=api("GET",photoPath());
            if(code==404) { photoPhase=Register;return; }
            if(code!=200) { if(code>=400&&code<500&&code!=429) photoError();return; }
        }
        uint32_t offset;
        if(!photoIdentity(true)||!getU32(doc["next_offset"],offset)||offset>photo.size||!doc["complete"].is<bool>()) { photoError();return; }
        photoOffset=offset;
        if(doc["complete"].as<bool>()) {
            if(offset!=photo.size) { photoError();return; }
            photoPhase=Commit;
        } else photoPhase=offset==photo.size?Complete:Read;
        return;
    }
    if(photoPhase==Read) {
        uint16_t n=photo.size-photoOffset>512?512:uint16_t(photo.size-photoOffset);
        uint8_t p[22];memcpy(p,photo.id,16);put32(p+16,photoOffset);put16(p+20,n);
        if(!rpc(PhotoRead,p,sizeof p)) { photoPhase=Idle;return; }
        if(reply.type==Error&&reply.length==2&&reply.data[1]==1) { photoWaitAt=millis();photoWaitMs=1000;return; }
        if(!photo.block(reply,photoOffset,n)) { photoError();return; }
        int code=api("PUT",photoPath()+"/chunks/"+String(photoOffset),reply.data+22,n);
        uint32_t next;
        if(code==200&&photoIdentity(false)&&getU32(doc["next_offset"],next)&&next==photoOffset+n) {
            photoOffset=next;photoPhase=next==photo.size?Complete:Read;
        } else if(code==507||code==413||code==422||code==415) photoError();
        else photoPhase=Query; // A timeout may follow a durable write. Reconcile, never guess.
        return;
    }
    if(photoPhase==Complete) {
        doc.clear();doc.to<JsonObject>();int code=api("POST",photoPath()+"/complete");
        if(code==200&&photoIdentity(true)&&doc["complete"].is<bool>()&&doc["complete"].as<bool>()) photoPhase=Commit;
        else if(code==409||code==422||code==507) photoError();
        else photoPhase=Query;
    }
}
} // namespace

void cameraSetup() {
    Serial.setRxBufferSize(1024);Serial.begin(115200,SERIAL_8N1);Serial.setDebugOutput(false);
    WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);
    uint8_t id[16];configured=CAMERA_CONFIGURED && strlen(CAMERA_WIFI_SSID)>0 &&
        strlen(CAMERA_DEVICE_TOKEN)>=32 && unhex(CAMERA_DEVICE_ID,id,16)&&!zeroId(id)&&strlen_P(CAMERA_CA_PEM)>64;
    if(!configured) { fault=Config;WiFi.mode(WIFI_OFF);return; }
    randomId(bootId);session=randomNonzero();
    trust=new BearSSL::X509List(CAMERA_CA_PEM);
    tls.setTrustAnchors(trust);tls.setSession(&tlsSession);tls.setTimeout(5000);
    // Keep default TLS RX buffer. Small application responses don't guarantee small TLS records.
    WiFi.begin(CAMERA_WIFI_SSID,CAMERA_WIFI_PASSWORD);lastWifi=millis();
    configTime(0,0,CAMERA_NTP_1,CAMERA_NTP_2);
    lastLink=millis()-5000;lastBeat=millis()-HeartbeatPeriod;lastPoll=millis()-2000;
}
void cameraLoop() {
    delay(1);if(!configured) return;
    uint32_t now=millis();
    if(WiFi.status()!=WL_CONNECTED) {
        beatConfirmed=false;beatDirty=true;
        if(elapsed(now,lastWifi,30000)) { lastWifi=now;WiFi.reconnect(); }
    }
    if(elapsed(now,lastLink,StatusPeriod)) {
        lastLink=now;if(hello()) readStatus();
    }
    // Collect already executed results even during Wi-Fi loss; don't lose them to a retry.
    if(guard.occupied && commandRunning && online && !eventPending && elapsed(now,lastResult,1000)) pollResult();
    if(!netReady()) return;
    if(beatDirty||!beatConfirmed||elapsed(now,lastBeat,HeartbeatPeriod)) { heartbeat();return; }
    if(guard.occupied) {
        if(eventPending) { sendEvent();return; }
        if(commandRunning && elapsed(now,leaseAt,leaseMs)) {
            commandRunning=false;newEvent("unknown","LEASE_EXPIRED");return;
        }
        if(commandRunning && elapsed(now,lastEvent,30000)) { newEvent("dispatched","OK");return; }
        return;
    }
    if(!online) return;
    if((caps&2)&&elapsed(now,lastPoll,2000)) { claimCommand();return; }
    if((caps&4)&&(!rtcAttempted||elapsed(now,lastRtc,3600000))) { syncRtc();return; }
    if(caps&8) photoStep();
}
