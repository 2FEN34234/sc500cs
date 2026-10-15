/* Included by rtc_diag.h under -Os, before the UI quiet-print macro. */
#ifndef RTC_SERIAL_H
#define RTC_SERIAL_H
#include <string.h>
#if !defined(RTC_HOST_TEST) || defined(RTC_UART_TEST)
#include "al_usart_ll.h"
#include "al_intr.h"
#include "nuclei_eclic.h"
#endif
static volatile unsigned rtc_rx_head,rtc_rx_tail,rtc_rx_error;
static volatile unsigned char rtc_rx[128];
static char rtc_line[32];
static unsigned rtc_line_len,rtc_line_bad,rtc_set_wait,rtc_set_seq;
static uint32_t rtc_set_tick;
static unsigned rtc_bcd(unsigned n) {return (n/10)*16+n%10;}
/* Exact ASCII format, Gregorian date validation, 2000..2099, Monday=1. */
static int rtc_parse(const char *p,uint32_t *t0,uint32_t *t1) {
    unsigned v[6]={0},n=0;
    static const unsigned char digits[6]={4,2,2,2,2,2};
    static const char sep[5]={'-','-',' ',':',':'};
    static const unsigned char days[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(strlen(p)!=19) return 0;
    for(unsigned i=0;i<6;i++) {
        for(unsigned j=0;j<digits[i];j++) {
            unsigned c=(unsigned char)p[n++];if(c<'0'||c>'9') return 0;
            v[i]=v[i]*10+c-'0';
        }
        if(i<5 && p[n++]!=sep[i]) return 0;
    }
    unsigned y=v[0],m=v[1],d=v[2];
    if(y<2000||y>2099||m<1||m>12||d<1||d>(unsigned)days[m-1]+(m==2 && y%4==0)
       ||v[3]>23||v[4]>59||v[5]>59) return 0;
    unsigned elapsed=(y-2000)*365+(y-2000+3)/4+d-1;
    for(unsigned i=1;i<m;i++) elapsed+=days[i-1]+(i==2 && y%4==0);
    *t0=(rtc_bcd(v[3])<<16)|(rtc_bcd(v[4])<<8)|rtc_bcd(v[5]);
    *t1=(rtc_bcd(y-2000)<<24)|(rtc_bcd(m)<<16)|(rtc_bcd(d)<<8)|((elapsed+5)%7+1);
    return 1;
}
#if !defined(RTC_HOST_TEST) || defined(RTC_UART_TEST)
/* RX runs during blocking SD/serial output too. No printing/APB in ISR. */
static void rtc_uart_irq(void *unused) {
    (void)unused;
    unsigned s=AlUsart_ll_GetStatus(USART1_BASE_ADDR);
    rtc_rx_error|=s&0x330u; // retain overflow/parity/frame/stop bits
    /* Clear the captured flags BEFORE draining: do not erase a new error
     * that arrives while the FIFO is being read. Never acknowledge TX EOT. */
    AL_REG32_WRITE(USART1_BASE_ADDR+USART_UART_STATUS_ADDR,s&0x20330u);
    for(unsigned i=0;i<32 && !(AlUsart_ll_GetStatus(USART1_BASE_ADDR)&(1u<<15));i++) {
        unsigned char c=AlUsart_ll_RecvData(USART1_BASE_ADDR);
        unsigned next=(rtc_rx_head+1)&127;
        if(next==rtc_rx_tail) rtc_rx_error|=0x1000000u; // software ring overflow
        else {rtc_rx[rtc_rx_head]=c;rtc_rx_head=next;}
    }
    if(s&(1u<<17)) AlUsart_ll_SetRxSize(USART1_BASE_ADDR,0xfffffu);
}
static void rtc_uart_init(void) {
    AlUsart_ll_DisableAllIntr(USART1_BASE_ADDR);
    AlUsart_ll_EnableRx(USART1_BASE_ADDR,AL_FALSE);
    AlUsart_ll_CleanRxFifo(USART1_BASE_ADDR,AL_TRUE);
    AlUsart_ll_CleanRxFifo(USART1_BASE_ADDR,AL_FALSE);
    AL_REG32_WRITE(USART1_BASE_ADDR+USART_UART_STATUS_ADDR,0x23330u);
    rtc_rx_head=rtc_rx_tail=rtc_rx_error=0;
    rtc_line_len=rtc_line_bad=0;
    AlUsart_ll_SetRxDmaEn(USART1_BASE_ADDR,AL_FALSE);
    AlUsart_ll_SetRxFifoWm(USART1_BASE_ADDR,0);
    AlUsart_ll_SetRxDataSize(USART1_BASE_ADDR,AL_USART_BYTE);
    AlUsart_ll_SetRxSize(USART1_BASE_ADDR,0xfffffu);
    AlUsart_ll_EnableRxDmaReload(USART1_BASE_ADDR,AL_TRUE);
    AlUsart_ll_EnableRx(USART1_BASE_ADDR,AL_TRUE);
    AlIntr_RegHandler(SOC_USART1_IRQn,AL_NULL,rtc_uart_irq,AL_NULL);
    AlUsart_ll_EnableIntr(USART1_BASE_ADDR,AL_USART_RXIE,AL_TRUE);
    AlUsart_ll_EnableIntr(USART1_BASE_ADDR,AL_USART_RX_EOT_IRQ_EN,AL_TRUE);
    AlIntr_SetLocalInterrupt(AL_FUNC_ENABLE);
}
#endif
static void rtc_command(uintptr_t base,const char *line) {
    volatile uint32_t *r=(volatile uint32_t *)base;
    uint32_t t0,t1,s;
    if(!strcmp(line,"RTC SYNC")) {al_printf("RTC: SYNC_OK\r\n");return;}
    if(!strcmp(line,"RTC GET")) {rtc_diag_last-=rtc_diag_period;return;}
    if(strncmp(line,"RTC SET ",8) || !rtc_parse(line+8,&t0,&t1)) {
        al_printf("RTC: REJECTED FORMAT_OR_DATE\r\n");return;
    }
    if(r[0x84/4]!=0x52544332u) {al_printf("RTC: REJECTED UPDATE_FPGA\r\n");return;}
    s=r[0x80/4];
    if(rtc_set_wait || (s&1)) {al_printf("RTC: REJECTED BUSY\r\n");return;}
    rtc_set_seq=s>>16;rtc_set_wait=1;rtc_set_tick=AlSys_GetTimerTickCount();
    r[0x78/4]=t0;r[0x7c/4]=t1;r[0x80/4]=0x53455431u;
    al_printf("RTC: SET_ACCEPTED %s\r\n",line+8);
}
static void rtc_serial_poll(uintptr_t base,uint32_t now) {
    if(rtc_rx_error) {
#if !defined(RTC_HOST_TEST) || defined(RTC_UART_TEST)
        ECLIC_DisableIRQ(SOC_USART1_IRQn);
#endif
        unsigned error=rtc_rx_error;
        rtc_rx_tail=rtc_rx_head;rtc_rx_error=0;rtc_line_len=0;rtc_line_bad=1;
#if !defined(RTC_HOST_TEST) || defined(RTC_UART_TEST)
        ECLIC_EnableIRQ(SOC_USART1_IRQn);
#endif
        al_printf("RTC: RX_ERROR flags=%08x\r\n",error);
    }
    /* Bound work so touch and SD keep being serviced under serial flooding. */
    for(unsigned i=0;i<128 && !rtc_rx_error && rtc_rx_tail!=rtc_rx_head;i++) {
        unsigned char c=rtc_rx[rtc_rx_tail];rtc_rx_tail=(rtc_rx_tail+1)&127;
        if(c=='\r'||c=='\n') {
            if(rtc_line_bad) al_printf("RTC: REJECTED LINE_ERROR\r\n");
            else if(rtc_line_len) {rtc_line[rtc_line_len]=0;rtc_command(base,rtc_line);}
            rtc_line_len=rtc_line_bad=0;
        } else if(c==8||c==127) {if(rtc_line_len) rtc_line_len--;}
        else if(c<32||c>126||rtc_line_len>=sizeof(rtc_line)-1) rtc_line_bad=1;
        else rtc_line[rtc_line_len++]=c;
    }
    if(rtc_set_wait==1) { /* owner 2 is the asynchronous Wi-Fi bridge */
        now=AlSys_GetTimerTickCount(); // command may have started during this poll
        volatile uint32_t *r=(volatile uint32_t *)base;
        uint32_t s=r[0x80/4];
        if(!(s&1) && (s>>16)!=rtc_set_seq && (s&2)) {
            rtc_set_wait=0;
            if(s&4) al_printf("RTC: SET_FAILED fault=%u status=%08x\r\n",(unsigned)((s>>4)&15),(unsigned)s);
            else al_printf("RTC: SET_OK verified=1 osf=0\r\n");
            rtc_diag_last-=rtc_diag_period;
        } else if(now-rtc_set_tick>rtc_diag_period*3) {
            rtc_set_wait=0;al_printf("RTC: SET_TIMEOUT status=%08x\r\n",(unsigned)s);
        }
    }
}
#endif
