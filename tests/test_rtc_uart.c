/* Exercise production ISR/init with the vendor LL header and MMIO FIFO model. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define RTC_HOST_TEST 1
#define RTC_UART_TEST 1
static unsigned hw[64],fifo[16],head,tail,used,inject_error;
static unsigned rtc_diag_last,rtc_diag_period=1000;
static uint32_t tick;
static uint32_t AlSys_GetTimerTickCount(void) {return tick;}
static char output[4096];
static void al_printf(const char *fmt,...) {
    va_list a;va_start(a,fmt);vsnprintf(output+strlen(output),sizeof(output)-strlen(output),fmt,a);va_end(a);
}
#include "../FDwork/shared/rtc_serial.h"
static unsigned uart_read(unsigned a) {
    if(a==USART_UART_STATUS_ADDR) return hw[a/4]|(used?2u:0x8000u);
    if(a==USART_UART_RXDATA_ADDR) {
        assert(used);unsigned c=fifo[tail];tail=(tail+1)%16;used--;
        hw[USART_UART_STATUS_ADDR/4]|=inject_error;inject_error=0;
        return c;
    }
    return hw[a/4];
}
static void uart_write(unsigned a,unsigned v) {
    if(a==USART_UART_STATUS_ADDR) hw[a/4]&=~v;
    else {hw[a/4]=v;if(a==USART_UART_SETUP_ADDR && (v&8)) head=tail=used=0;}
}
static void receive(unsigned c) {assert(used<16);fifo[head]=c;head=(head+1)%16;used++;}
int main(void) {
    uint32_t apb[40]={0};apb[0x84/4]=0x52544332;
    hw[USART_UART_STATUS_ADDR/4]=0x33330;receive('X');
    rtc_rx_error=1;rtc_rx_head=4;rtc_uart_init();
    assert(!used && !rtc_rx_error && !rtc_rx_head);
    assert(!(hw[USART_UART_STATUS_ADDR/4]&0x23330));
    assert(hw[USART_UART_STATUS_ADDR/4]&0x10000); // preserve TX completion
    assert(irq_enabled && local_enabled && registered_irq==rtc_uart_irq);
    assert(AlUsart_ll_GetRxFifoWm(0)==0 && AlUsart_ll_GetRxEnState(0));
    assert(AlUsart_ll_GetRxSize(0)==0xfffff);
    const char *command="RTC SET 2026-10-14 00:04:00\r\n";
    for(const char *p=command;*p;p++) {receive(*p);registered_irq(NULL);}
    rtc_serial_poll((uintptr_t)apb,0);
    assert(rtc_set_wait && apb[0x78/4]==0x400 && apb[0x7c/4]==0x26101403);
    assert(strstr(output,"SET_ACCEPTED"));rtc_set_wait=0;
    /* An error arising during a read must survive the IRQ's W1C. */
    receive('x');inject_error=0x100;rtc_uart_irq(NULL);
    assert(hw[USART_UART_STATUS_ADDR/4]&0x100);
    rtc_uart_irq(NULL);assert(rtc_rx_error==0x100);
    rtc_serial_poll((uintptr_t)apb,0);assert(strstr(output,"flags=00000100"));
    assert(irq_enabled && rtc_line_bad);
    receive('\n');rtc_uart_irq(NULL);rtc_serial_poll((uintptr_t)apb,0);assert(!rtc_line_bad);
    for(const char *p="RTC SYNC\n";*p;p++) {receive(*p);rtc_uart_irq(NULL);}
    rtc_serial_poll((uintptr_t)apb,0);assert(strstr(output,"SYNC_OK"));
    /* Ring overflow is distinct from hardware parity/frame/overflow. */
    for(unsigned i=0;i<128;i++) {receive('A');rtc_uart_irq(NULL);}
    assert(rtc_rx_error==0x1000000);
    rtc_serial_poll((uintptr_t)apb,0);assert(!rtc_set_wait);
    hw[USART_UART_STATUS_ADDR/4]|=0x20000;rtc_uart_irq(NULL);
    assert(!(hw[USART_UART_STATUS_ADDR/4]&0x20000));
    puts("PASS vendor LL UART init, stale FIFO/flags, burst RX, error preservation, resync, ring overflow, EOT");
}
