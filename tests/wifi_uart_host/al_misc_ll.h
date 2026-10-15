#define AL_MISC_USART0 1
#define MISC_CTRL_BASE_ADDR 0x18000000u
#define SOC_MISC_CTRL__SUBM_RESET_CTRL0__ADDR 0x20u
#define SOC_MISC_CTRL__SUBM_CLK_CTRL0__ADDR 0x40u
#define SOC_MISC_CTRL__CLK_CTRL3_USART0_CLK_I__ADDR 0x10cu
static unsigned wifi_test_clock,wifi_test_reset_count,wifi_test_reset;
static unsigned wifi_test_settle,wifi_test_fenced;
static void AlMisc_ll_SetClkEn(unsigned id,unsigned value) {
    assert(id==AL_MISC_USART0);wifi_test_clock=value;wifi_test_settle=wifi_test_fenced=0;
}
static void AlMisc_ll_SetReset(unsigned id,unsigned value) {
    assert(id==AL_MISC_USART0);
    if(value) assert(wifi_test_clock && wifi_test_fenced && wifi_test_settle>=10);
    wifi_test_reset=value;wifi_test_reset_count++;wifi_test_settle=wifi_test_fenced=0;
}
#define __RWMB() ((void)(wifi_test_fenced=1))
static void AlSys_UDelay(uint64_t us) {
    assert(wifi_test_fenced);wifi_test_settle+=(unsigned)us;
}
