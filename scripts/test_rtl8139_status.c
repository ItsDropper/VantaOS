#include "rtl8139_regs.h"
#include <stdio.h>

int main(void)
{
    if (rtl8139_tsd_is_terminal(0x00002000U)) {
        fputs("host-ownership bit was mistaken for TX completion\n", stderr);
        return 1;
    }
    if (rtl8139_tsd_is_success(0x00002000U)) {
        fputs("host-ownership bit was mistaken for TX success\n", stderr);
        return 2;
    }
    if (!rtl8139_tsd_is_terminal(RTL8139_TSD_TOK) ||
        !rtl8139_tsd_is_success(RTL8139_TSD_TOK)) {
        fputs("TOK was not recognized as successful completion\n", stderr);
        return 3;
    }

    const uint32_t errors[] = {
        RTL8139_TSD_TUN,
        RTL8139_TSD_OUT_OF_WINDOW,
        RTL8139_TSD_ABORTED,
        RTL8139_TSD_CARRIER_LOST
    };
    for (unsigned int i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        if (!rtl8139_tsd_is_terminal(errors[i]) ||
            rtl8139_tsd_is_success(errors[i]) ||
            rtl8139_tsd_is_success(errors[i] | RTL8139_TSD_TOK)) {
            fputs("a terminal transmit error was not rejected\n", stderr);
            return 4;
        }
    }

    puts("RTL8139 TSD completion and error-status tests passed");
    return 0;
}
