/* Actual USART0 init/ISR/outer poll, vendor LL header and a finite FIFO model. */
#define WIFI_MCU_UART_TEST
#define USART0_BASE_ADDR 0
#define SOC_USART0_IRQn 0
#define PH1P35_MCU_USART_CLOCK 300000000u
#define al_simp_printf printf
#include "al_usart_ll.h"
#include "al_intr.h"
#include "nuclei_eclic.h"
#define main wifi_logic_tests_main
#include "test_wifi_mcu.c"
#undef main
static unsigned hw[64],rx_fifo[16],rx_head,rx_tail,rx_used;
static unsigned tx_used,tx_expected,tx_done,tx_length;
static uint8_t tx_wire[600];
static unsigned uart_read(unsigned a) {
    if(a==MISC_CTRL_BASE_ADDR+SOC_MISC_CTRL__SUBM_RESET_CTRL0__ADDR) return wifi_test_reset<<AL_MISC_USART0;
    if(a==MISC_CTRL_BASE_ADDR+SOC_MISC_CTRL__SUBM_CLK_CTRL0__ADDR) return wifi_test_clock<<AL_MISC_USART0;
    if(a==MISC_CTRL_BASE_ADDR+SOC_MISC_CTRL__CLK_CTRL3_USART0_CLK_I__ADDR) return 0;
    assert(wifi_test_clock && wifi_test_reset && wifi_test_fenced && wifi_test_settle>=10);
    if(a==USART_UART_STATUS_ADDR) return hw[a/4]|(rx_used?2u:0x8000u)|(tx_used>=16?0x4000u:1u);
    if(a==USART_UART_RXDATA_ADDR) {assert(rx_used);unsigned c=rx_fifo[rx_tail++%16];rx_used--;return c;}
    return hw[a/4];
}
static void uart_write(unsigned a,unsigned v) {
    /* A disabled/unsettled peripheral must not be accessed, even for IRQ=0. */
    assert(wifi_test_clock && wifi_test_reset && wifi_test_fenced && wifi_test_settle>=10);
    if(a==USART_UART_STATUS_ADDR) hw[a/4]&=~v;
    else if(a==USART_UART_TXDATA_ADDR) {
        assert(tx_used<16 && tx_length<sizeof tx_wire);tx_used++;tx_wire[tx_length++]=v;
    } else {
        hw[a/4]=v;
        if(a==USART_UART_SETUP_ADDR && (v&8)) rx_head=rx_tail=rx_used=0;
        if(a==USART_UART_TX_SIZE_ADDR) {tx_expected=v&0xfffffu;tx_done=tx_length=tx_used=0;}
    }
}
static void receive(uint8_t c) {assert(rx_used<16);rx_fifo[rx_head++%16]=c;rx_used++;}
static void drain_tx(void) {
    unsigned limit=2000;
    while(tx_done<tx_expected && --limit) {
        if(AlUsart_ll_GetIntrCfg(0)&1) wifi_uart_irq(0);
        if(tx_used) {tx_used--;tx_done++;}
    }
    assert(limit);hw[USART_UART_STATUS_ADDR/4]|=1u<<16;
}
static void send_request(unsigned type,uint32_t seq,const uint8_t *payload,unsigned len) {
    uint8_t raw[42]={0x57,0x46,1},wire[64];assert(len<=24);
    raw[3]=type;wf_put32(raw+4,2);wf_put32(raw+8,seq);wf_put16(raw+12,len);
    if(len) memcpy(raw+14,payload,len);
    wf_put32(raw+14+len,wf_crc(raw,14+len));
    unsigned n=wf_encode(wire,raw,18+len);
    for(unsigned i=0;i<n;i++) {receive(wire[i]);wifi_uart_irq(0);}
    wifi_mcu_poll(0);
}
static void check_time_reply(uint32_t seq,unsigned code,unsigned verified) {
    uint8_t decoded[64];assert(wifi_tx_started);
    drain_tx();wifi_mcu_poll(0);
    assert(tx_length>=2 && tx_length-2<=sizeof decoded);
    memcpy(decoded,tx_wire+1,tx_length-2);
    assert(wf_decode(decoded,tx_length-2)==20); /* includes CRC validation */
    assert(decoded[3]==0x85 && wf_get16(decoded+12)==2);
    assert(wf_get32(decoded+4)==2 && wf_get32(decoded+8)==seq);
    assert(decoded[14]==code && decoded[15]==verified);
    assert(!wifi_link.pending && !rtc_set_wait && !wifi_tx_started);
}
static void test_rtc_uart_async(void) {
    uint8_t payload[10]={0};wf_put32(payload,1791979200u);wf_put16(payload+8,480);
    regs[0x84/4]=0x52544332u;
    for(unsigned scenario=0;scenario<4;scenario++) {
        uint32_t seq=2+scenario,old=40+scenario;
        unsigned code=scenario==1?WF_RTC_VERIFY_FAILED:scenario==2?WF_IN_PROGRESS:WF_OK;
        regs[0x80/4]=(old<<16)|2;rtc_set_wait=0;
        if(scenario==3) tick=(uint64_t)(UINT32_MAX-1000u)*(PH1P35_MCU_TIMER_CLOCK/1000u);
        send_request(5,seq,payload,sizeof payload);
        assert(wifi_link.pending && rtc_set_wait==2 && !wifi_tx_started);
        assert(regs[0x80/4]==0x53455431u && wifi_rtc_seq==old);
        /* Make rx differ from the result: response must come from reply. */
        wifi_link.rx[14]=0xA6;wifi_link.rx[15]=0x5B;
        regs[0x80/4]=(old<<16)|2;wifi_mcu_poll(0);
        assert(wifi_link.pending && !wifi_tx_started); /* stale completion */
        regs[0x80/4]=((old+1)<<16)|3;wifi_mcu_poll(0);
        assert(wifi_link.pending && !wifi_tx_started); /* still busy */
        tick+=(uint64_t)(scenario==2?3501:1500)*(PH1P35_MCU_TIMER_CLOCK/1000u);
        if(scenario!=2) regs[0x80/4]=((old+1)<<16)|(scenario==1?6u:2u);
        wifi_mcu_poll(0);check_time_reply(seq,code,code==WF_OK);
        /* Same request must replay the actual result, not trigger another set,
         * even if the hardware status has since changed. */
        regs[0x80/4]=((old+2)<<16)|6;
        send_request(5,seq,payload,sizeof payload);
        check_time_reply(seq,code,code==WF_OK);
        assert(regs[0x80/4]==(((old+2)<<16)|6));
    }
}
int main(void) {
    wifi_logic_tests_main();
    memset(&wifi_link,0,sizeof wifi_link);memset(wifi_commands,0,sizeof wifi_commands);
    rtc_set_wait=0;wifi_enabled=0;wifi_in_service=0;
    regs[0x90/4]=0;wifi_mcu_init();assert(!wifi_enabled && !wifi_test_reset_count);
    regs[0x90/4]=0x57494631u;wifi_mcu_init();
    assert(wifi_enabled && wifi_boot_id && wifi_test_clock && wifi_test_reset_count==2);
    uint32_t previous=wifi_boot_id;wifi_mcu_init();assert(wifi_boot_id!=previous);
    assert(AlUsart_ll_GetTxEnState(0) && AlUsart_ll_GetRxEnState(0));
    assert(AlUsart_ll_GetStopBit(0)==AL_USART_STOP_1_BIT && regs[0x88/4]==0);
    // Deliver HELLO through real IRQ then outer poll.
    uint8_t raw[18]={0x57,0x46,1,1},wire[64];wf_put32(raw+4,1);wf_put32(raw+8,1);wf_put32(raw+14,wf_crc(raw,14));
    unsigned n=wf_encode(wire,raw,18);
    for(unsigned i=0;i<n;i++) {receive(wire[i]);wifi_uart_irq(0);}
    wifi_mcu_poll(0);assert(wifi_tx_started);drain_tx();wifi_mcu_poll(0);
    uint8_t decoded[567];memcpy(decoded,tx_wire+1,tx_length-2);
    assert(wf_decode(decoded,tx_length-2)==26 && decoded[3]==0x81);
    // ESP checks every five seconds. Retries/session changes are not MCU boots.
    const uint32_t startup_id=wifi_boot_id;
    uint8_t highwater[16];memcpy(highwater,wifi_command_highwater,16);
    wifi_commands[0].used=1;wifi_commands[0].state=1;
    wifi_commands[0].id[0]=0xA5;
    WifiCommand retained=wifi_commands[0];
    assert(wf_get32(decoded+14)==startup_id && startup_id);
    for(unsigned probe=0;probe<22;probe++) {
        uint32_t session=probe==21?2:1;
        uint32_t sequence=probe==21?1:(probe==20?21:probe+2);
        tick+=(uint64_t)PH1P35_MCU_TIMER_CLOCK*5;
        wf_put32(raw+4,session);wf_put32(raw+8,sequence);
        wf_put32(raw+14,wf_crc(raw,14));n=wf_encode(wire,raw,18);
        for(unsigned i=0;i<n;i++) {receive(wire[i]);wifi_uart_irq(0);}
        wifi_mcu_poll(0);assert(wifi_tx_started);drain_tx();wifi_mcu_poll(0);
        memcpy(decoded,tx_wire+1,tx_length-2);
        assert(wf_decode(decoded,tx_length-2)==26 && decoded[3]==0x81);
        assert(wf_get32(decoded+4)==session && wf_get32(decoded+8)==sequence);
        assert(wf_get32(decoded+14)==startup_id && wifi_boot_id==startup_id);
        assert(wf_get32(decoded+18)==15);
        assert(!memcmp(highwater,wifi_command_highwater,16));
        assert(!memcmp(&retained,&wifi_commands[0],sizeof retained));
    }
    test_rtc_uart_async();
    // Full photo reply drains entirely via interrupts while main is blocked.
    for(unsigned i=0;i<534;i++) wifi_link.reply[14+i]=(uint8_t)i;
    wifi_reply(&wifi_link,0x87,534);wifi_mcu_poll(1);
    assert(wifi_tx_started);drain_tx();assert(wifi_link.tx_pos==wifi_link.tx_len);
    wifi_mcu_poll(0);assert(!wifi_tx_started);
    memcpy(decoded,tx_wire+1,tx_length-2);assert(wf_decode(decoded,tx_length-2)==552);
    for(unsigned i=0;i<534;i++) assert(decoded[14+i]==(uint8_t)i);
    // Hardware errors and software overflow discard partial input, then resync.
    hw[USART_UART_STATUS_ADDR/4]|=0x100;wifi_uart_irq(0);assert(wifi_rx_fault);
    wifi_mcu_poll(0);assert(!wifi_rx_fault && wifi_link.drop);
    for(unsigned i=0;i<128;i++) {receive(1);wifi_uart_irq(0);}
    assert(wifi_rx_fault);wifi_mcu_poll(0);assert(wifi_rx_tail==wifi_rx_head);
    puts("PASS USART0 capability gate, init, stable boot ID/ledger, real TIME_SET IRQ/poll asynchronous success/failure/timeout/timer-wrap and cached replay, 534-byte reply, errors, overflow");
}
