#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#define WIFI_MCU_HOST_TEST
#define RTC_HOST_TEST
#define PH1P35_MCU_TIMER_CLOCK 100000000u
#define al_printf(...) ((void)0)
static uint64_t tick;
uint64_t AlSys_GetTimerTickCount(void) {return tick;}
static uint32_t rtc_diag_period,rtc_diag_last;
#include "../FDwork/shared/rtc_serial.h"
#include "../FDwork/shared/menu_input.h"
#include "../FDwork/shared/sd_recorder.h"
static MenuState ui={.exposure=3990,.gain=80},*record_ui=&ui;
static uint32_t regs[40],flags,interval=2,commits,photos;
volatile uint32_t g_rec_state,g_rec_error,g_rec_saved;
static uint32_t ui_read(unsigned off) {return regs[off/4];}
static void ui_write(unsigned off,uint32_t v) {regs[off/4]=v;}
unsigned recorder_wifi_flags(void) {return flags;}
unsigned recorder_interval(void) {return interval;}
int recorder_available(void) {return 1;}
void recorder_last_photo_id(uint8_t id[16]) {memset(id,0x44,16);}
unsigned recorder_photo_next(uint8_t *p,unsigned *n) {(void)p;(void)n;photos++;return 6;}
unsigned recorder_photo_read(const uint8_t id[16],uint32_t o,unsigned n,uint8_t *d) {(void)id;(void)o;(void)n;(void)d;photos++;return 5;}
unsigned recorder_photo_commit(const uint8_t id[16],uint32_t s,uint32_t c) {(void)id;(void)s;(void)c;photos++;return 5;}
static void menu_commit(MenuState *s,const MenuState *old,unsigned actions) {
    (void)old;commits++;
    if(actions&MENU_PHOTO) flags|=8;
    if(actions&MENU_RECORD) flags|=9;
    if(actions&MENU_STOP) flags&=~1u;
    if(actions&MENU_INTERVAL) {const unsigned v[5]={2,5,10,30,60};interval=v[s->record_interval];}
}
#include "../FDwork/shared/wifi_mcu.h"
static uint8_t out[544],rtype;
static int call(unsigned type,const uint8_t *p,unsigned n) {rtype=type|128;return wifi_dispatch(type,p,n,out,&rtype);}
int main(void) {
    unsigned v[6];
    assert(wf_epoch_calendar(1791979200ULL,480,v) && v[0]==2026 && v[1]==10 && v[2]==14);
    assert(wf_epoch_calendar(951753600ULL,480,v) && v[0]==2000 && v[1]==2 && v[2]==29);
    assert(!wf_epoch_calendar(UINT64_MAX,480,v));
    assert(!wf_epoch_calendar(946684800,841,v));
    assert(!wf_epoch_calendar(946684800,-1,v));
    // Exhaustive day round-trip across the supported century with 8h offset.
    for(uint32_t days=0;days<36525;days++) {
        uint64_t epoch=946684800ULL+(uint64_t)days*86400;
        assert(wf_epoch_calendar(epoch,480,v));
        uint32_t stamp=((v[0]-1980)<<25)|(v[1]<<21)|(v[2]<<16)|(v[3]<<11);
        assert(wf_fat_epoch(stamp)==epoch);
    }
    wifi_boot_id=42;assert(call(1,0,0)==8 && wf_get32(out)==42 && wf_get32(out+4)==15);
    regs[0x68/4]=2;flags=4;assert(call(2,0,0)==12 && wf_get16(out+8)==6);
    uint8_t cmd[21]={1};cmd[16]=1;
    assert(call(3,cmd,17)==17 && out[16]==0 && commits==1);
    assert(call(3,cmd,17)==17 && out[16]==0 && commits==1);
    cmd[16]=4;call(3,cmd,17);assert(out[16]==7 && commits==1);cmd[16]=1;
    call(4,cmd,16);assert(out[16]==0);
    // During a yielded mount, transient idle must not fail the accepted capture.
    flags=0;wifi_in_service=1;call(4,cmd,16);assert(out[16]==0);
    assert(call(6,0,0)==1 && out[0]==WF_NO_PHOTO && photos==1);
    uint8_t read_request[24]={0};wf_put16(read_request+20,512);
    assert(call(7,read_request,22)==2 && rtype==255 && out[1]==WF_BUSY && photos==1);
    assert(call(8,read_request,24)==17 && out[16]==WF_BUSY && photos==1);
    wifi_in_service=0;g_rec_saved=1;call(4,cmd,16);assert(out[16]==1 && out[19]==0x44);
    cmd[0]=2;cmd[16]=2;wf_put16(cmd+17,1000);wf_put16(cmd+19,80);
    call(3,cmd,21);assert(out[16]==0 && ui.exposure==1000);
    cmd[0]=3;wf_put16(cmd+17,3991);call(3,cmd,21);assert(out[16]==WF_BAD_ARGUMENT && ui.exposure==1000);
    wf_put16(cmd+17,1000);
    cmd[0]=3;wf_put16(cmd+19,0);call(3,cmd,21);assert(out[16]==2 && ui.gain==80);
    cmd[16]=3;wf_put32(cmd+17,30);call(3,cmd,21);assert(!out[16] && interval==30);
    cmd[0]=4;cmd[16]=4;call(3,cmd,17);assert(!out[16]);
    flags=0;call(4,cmd,16);assert(out[16]==1);
    // Many commands beyond cache capacity succeed. Evicted IDs never replay.
    for(unsigned i=5;i<=65;i++) {cmd[0]=i;cmd[16]=2;wf_put16(cmd+17,1000);wf_put16(cmd+19,80);call(3,cmd,21);assert(!out[16]);}
    unsigned done=commits;
    cmd[0]=64;call(3,cmd,21);assert(!out[16] && commits==done);
    cmd[0]=2;call(3,cmd,21);assert(out[16]==WF_UNKNOWN_ID && commits==done);
    call(1,0,0);call(3,cmd,21);assert(out[16]==WF_UNKNOWN_ID && commits==done);
    call(4,cmd,16);assert(out[16]==3);
    // Sustained operation rolls over the result cache without resetting MCU.
    cmd[0]=66;cmd[16]=2;done=commits;
    for(unsigned i=1;i<=1024;i++) {
        cmd[14]=i>>8;cmd[15]=i;call(3,cmd,21);assert(out[16]==WF_OK);
    }
    assert(commits==done+1024);
    call(3,cmd,21);assert(out[16]==WF_OK && commits==done+1024);
    cmd[14]=0;cmd[15]=1;call(3,cmd,21);
    assert(out[16]==WF_UNKNOWN_ID && commits==done+1024);
    // Real accepted STOP operations stay pending while the recorder is busy.
    // Only pending entries fill the cache; none may be evicted to accept more.
    cmd[0]=67;cmd[14]=0;cmd[16]=4;flags=8;
    for(unsigned i=1;i<=WIFI_HISTORY;i++) {
        cmd[15]=i;call(3,cmd,17);assert(out[16]==WF_OK);
    }
    done=commits;uint8_t watermark[16];memcpy(watermark,wifi_command_highwater,16);
    cmd[15]=WIFI_HISTORY+1;call(3,cmd,17);
    assert(out[16]==WF_BUSY && commits==done && !memcmp(watermark,wifi_command_highwater,16));
    for(unsigned i=0;i<WIFI_HISTORY;i++) assert(wifi_commands[i].used && !wifi_commands[i].state);
    flags=0;wifi_commands_poll(); /* safe stop boundary frees terminal results */
    call(3,cmd,17);assert(out[16]==WF_OK && commits==done+1);
    cmd[15]=1;call(3,cmd,17);assert(out[16]==WF_UNKNOWN_ID && commits==done+1);
    uint8_t time[10]={0};wf_put32(time,1791979200u);wf_put16(time+8,480);regs[0x84/4]=0x52544332u;
    assert(call(5,time,10)==-1 && rtc_set_wait==2 && regs[0x80/4]==0x53455431u);
    assert(call(5,time,10)==2 && out[0]==WF_BUSY);
    rtc_serial_poll((uintptr_t)regs,0);assert(rtc_set_wait==2); // PC does not steal completion
    // Real asynchronous completion must match the new hardware sequence.
    memset(&wifi_link,0,sizeof wifi_link);wifi_link.last[0]=0x57;wifi_link.last[1]=0x46;wifi_link.last[2]=1;
    wifi_link.pending=1;regs[0x80/4]=2;wifi_pending_poll();assert(wifi_link.pending);
    regs[0x80/4]=0x10002;wifi_pending_poll();assert(!wifi_link.pending && !rtc_set_wait);
    uint8_t decoded[64];memcpy(decoded,wifi_link.reply+1,wifi_link.reply_len-2);
    assert(wf_decode(decoded,wifi_link.reply_len-2)==20 && decoded[14]==0 && decoded[15]==1);
    wifi_link.pending=1;rtc_set_wait=2;regs[0x80/4]=0x10006;wifi_pending_poll();
    memcpy(decoded,wifi_link.reply+1,wifi_link.reply_len-2);
    assert(wf_decode(decoded,wifi_link.reply_len-2)==20 && decoded[14]==WF_RTC_VERIFY_FAILED && !decoded[15]);
    wifi_link.pending=1;rtc_set_wait=2;regs[0x80/4]=1;tick=400000000;
    wifi_pending_poll();memcpy(decoded,wifi_link.reply+1,wifi_link.reply_len-2);
    assert(wf_decode(decoded,wifi_link.reply_len-2)==20 && decoded[14]==WF_IN_PROGRESS && !decoded[15]);
    rtc_set_wait=0;wf_put32(time+4,1);assert(call(5,time,10)==2 && out[0]==WF_BAD_ARGUMENT);
    puts("PASS Wi-Fi commands, 1024 additional actions without reset, pending-cache BUSY/recovery, evicted IDs no replay, SD yield isolation, RTC ownership, calendar century");
}
