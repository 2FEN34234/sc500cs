/* Exercise the actual driver on connection IRQs, including a reconnect at
 * the same speed. Does not emulate descriptor bytes or verify physical USB. */
#define MAX_RECONNECT_MODEL
#define main legacy_probe_test_main
#include "test_max3421e_diag.c"
#undef main
#ifndef USB_PRODUCTION
#define USB_PRODUCTION "../FDwork/anlu_app/max3421e_diag.c"
#endif
#include USB_PRODUCTION

static void start(unsigned old_bus, unsigned current_bus, unsigned irq)
{
    fault = NORMAL;
    calls = rx_pending = rx_done = tx_done = selected = 0;
    memset((void *)board, 0, 4096);
    board[USB_CAP/4] = USB_CAP_VALUE;
    assert(max3421e_diag_init(0x70000000u) == 0);
    host_bus_state = (uint8_t)old_bus;
    host_descriptor_done = 1;
    hid_endpoint_count = 0;
    touch_decoder.down = 1;
    regs[REG_HRSL >> 3] = (uint8_t)current_bus;
    regs[REG_HIRQ >> 3] = (uint8_t)irq;
    usb_bus_resets = 0;
}

int main(void)
{
    TouchEvent event;
    board = VirtualAlloc((void *)0x70000000, 4096,
                         MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(board == (void *)0x70000000);
    start(HRSL_JSTATUS, HRSL_JSTATUS, CONDETIRQ);
    max3421e_diag_report();
    assert(usb_bus_resets == 1); /* Previously silently disabled HID forever. */
    assert(max3421e_touch_pop(&event) && event.type == TOUCH_CANCEL);
    assert(!(regs[REG_HIRQ >> 3] & CONDETIRQ));

    start(0, HRSL_JSTATUS, CONDETIRQ);
    max3421e_diag_report();
    assert(usb_bus_resets == 1);

    start(HRSL_JSTATUS, 0, CONDETIRQ);
    max3421e_diag_report();
    assert(usb_bus_resets == 0 && host_descriptor_done == 0);
    assert(max3421e_touch_pop(&event) && event.type == TOUCH_CANCEL);

    start(HRSL_JSTATUS, HRSL_JSTATUS, 0);
    max3421e_diag_report();
    assert(usb_bus_resets == 0 && host_descriptor_done == 1);
    VirtualFree((void *)board, 0, MEM_RELEASE);
    puts("PASS: same-speed reconnect retries enumeration; attachment, detach and stable bus handled");
    return 0;
}
