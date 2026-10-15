#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
static char output[4096];
static uint64_t tick;
static uint32_t regs[40];
#define RTC_HOST_TEST 1
#define PH1P35_MCU_TIMER_CLOCK 1000
static void rtc_uart_init(void) {}
static void log_printf(const char *format,...) {
    va_list args;va_start(args,format);
    vsnprintf(output+strlen(output),sizeof(output)-strlen(output),format,args);
    va_end(args);
}
#define al_printf log_printf
#include "../FDwork/shared/rtc_diag.h"
uint64_t AlSys_GetTimerFreq(void) {return 1000;}
uint64_t AlSys_GetTimerTickCount(void) {return tick;}
static void poll(void) {
    uint32_t before[40];memcpy(before,regs,sizeof regs);
    output[0]=0;tick+=1000;rtc_diag_poll((uintptr_t)regs);
    assert(memcmp(before,regs,sizeof regs)==0); // diagnostics must never set RTC
}
static void serial(const char *s) {
    while(*s) {unsigned next=(rtc_rx_head+1)&127;assert(next!=rtc_rx_tail);rtc_rx[rtc_rx_head]=*s++;rtc_rx_head=next;}
    rtc_serial_poll((uintptr_t)regs,(uint32_t)tick);
}
int main(void) {
    regs[0x60/4]=0x00235958;regs[0x64/4]=0x26101302;
    regs[0x68/4]=0x00010002;
    rtc_diag_init((uintptr_t)regs);
    assert(strstr(output,"rtc-set-v3") && strstr(output,"2026-10-13 23:59:58 trusted=1"));
    output[0]=0;rtc_diag_poll((uintptr_t)regs);assert(!output[0]);
    regs[0x68/4]=0x00020080;poll();assert(strstr(output,"health=2") && strstr(output,"trusted=0"));
    const char *faults[]={"fault=1","fault=2","fault=3","fault=4"};
    for(unsigned i=1;i<=4;i++) {
        regs[0x68/4]=4|(i<<3);poll();assert(strstr(output,faults[i-1]));
    }
    regs[0x68/4]=0x00030002;regs[0x60/4]=0x00520000;poll();
    assert(strstr(output,"00:00:00 trusted=1")); // 12 AM
    regs[0x60/4]=0x00720000;regs[0x64/4]=0x26810102;poll();
    assert(strstr(output,"2126-01-01 12:00:00")); // 12 PM and century bit
    regs[0x60/4]=0x00125a00;poll();assert(strstr(output,"health=4") && strstr(output,"trusted=0"));
    regs[0x60/4]=0x00120001;regs[0x68/4]=0x00040002;poll();
    tick+=5000;poll();assert(strstr(output,"health=5") && strstr(output,"trusted=0"));
    regs[0x60/4]=0x00120002;poll();
    for(unsigned i=5;i<=10;i++) {regs[0x68/4]=(i<<16)|2;poll();}
    assert(strstr(output,"health=6") && strstr(output,"trusted=0"));
    regs[0x60/4]=0x00120003;regs[0x68/4]+=0x10000;poll();
    assert(strstr(output,"health=0") && strstr(output,"trusted=1"));
    puts("PASS RTC log throttling, BCD/12h/century, OSF/I2C faults, stale/frozen recovery, read-only access");
    uint32_t t0,t1;
    assert(rtc_parse("2026-10-13 23:59:58",&t0,&t1) && t0==0x235958 && t1==0x26101302);
    assert(rtc_parse("2000-01-01 00:00:00",&t0,&t1) && t1==0x00010106);
    assert(rtc_parse("2028-02-29 12:00:00",&t0,&t1));
    const char *bad[]={"2026-02-29 00:00:00","2026-04-31 00:00:00","2100-01-01 00:00:00",
        "2026-10-13 24:00:00","2026-10-13 12:60:00","2026-10-13 12:00:60","2026-00-01 00:00:00",
        "2026-10-13T12:00:00","2026-10-13 12:00:00x","2026"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) assert(!rtc_parse(bad[i],&t0,&t1));
    output[0]=0;rtc_command((uintptr_t)regs,"RTC SET 2026-10-13 23:59:58");
    assert(strstr(output,"UPDATE_FPGA"));
    regs[0x84/4]=0x52544332;regs[0x80/4]=0;
    rtc_command((uintptr_t)regs,"RTC SET 2026-10-13 23:59:58");
    assert(regs[0x78/4]==0x235958 && regs[0x7c/4]==0x26101302 && regs[0x80/4]==0x53455431 && rtc_set_wait);
    output[0]=0;regs[0x80/4]=2;rtc_serial_poll((uintptr_t)regs,tick);assert(!strstr(output,"SET_OK"));
    regs[0x80/4]=0x10002;rtc_serial_poll((uintptr_t)regs,tick);assert(strstr(output,"SET_OK")&&!rtc_set_wait);
    rtc_command((uintptr_t)regs,"RTC SET 2026-10-13 23:59:58");
    regs[0x80/4]=0x20056;output[0]=0;rtc_serial_poll((uintptr_t)regs,tick);
    assert(strstr(output,"SET_FAILED fault=5")&&!rtc_set_wait);
    puts("PASS strict calendar parser, weekday, old-bit protection, set sequence and error reporting");
    output[0]=0;regs[0x80/4]=0x20002;
    serial("RTC SET 2026-10-13 ");assert(!rtc_set_wait);
    serial("23:59:58\r\n");assert(rtc_set_wait && strstr(output,"SET_ACCEPTED"));
    assert(!strstr(output,"TIMEOUT"));
    serial("RTC SET 2026-10-13 23:59:58\n");assert(strstr(output,"BUSY"));
    regs[0x80/4]=0x30002;rtc_serial_poll((uintptr_t)regs,(uint32_t)tick);assert(!rtc_set_wait);
    output[0]=0;serial("RTC SET 2026-10-13 23:59:58garbagegarbage\n");
    assert(!rtc_set_wait && strstr(output,"LINE_ERROR"));
    output[0]=0;serial("RTC SET 2026-10-13 23:59:58\x18\n");assert(!rtc_set_wait && strstr(output,"LINE_ERROR"));
    output[0]=0;rtc_rx_error=1;serial("RTC SET 2026-10-13 23:59:58\n");
    assert(!rtc_set_wait && strstr(output,"RX_ERROR"));serial("\n");
    // Real frequency, 32-bit timer crossing rollover, no premature timeout.
    rtc_diag_period=100000000;tick=0xfffffff0u;
    serial("RTC SET 2026-10-13 23:59:58\n");regs[0x80/4]=0x30001;
    tick+=100000000;output[0]=0;rtc_serial_poll((uintptr_t)regs,(uint32_t)tick);
    assert(rtc_set_wait && !strstr(output,"TIMEOUT"));
    tick+=300000001;rtc_serial_poll((uintptr_t)regs,(uint32_t)tick);
    assert(!rtc_set_wait && strstr(output,"TIMEOUT"));
    puts("PASS fragmented/CRLF lines, BUSY, overflow, cancel, RX fault and timer rollover");
}
