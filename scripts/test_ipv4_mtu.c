#include "net_ipv4.h"
#include <stdint.h>
#include <stdio.h>

static int expect(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        return 0;
    }
    return 1;
}

int main(void)
{
    int ok = 1;
    ok &= expect(net_ipv4_payload_fits_mtu(NET_MTU_MIN, 1U), "minimum MTU accepts payload");
    ok &= expect(net_ipv4_payload_fits_mtu(NET_MTU_MIN, NET_MTU_MIN - 20U), "minimum MTU exact limit");
    ok &= expect(!net_ipv4_payload_fits_mtu(NET_MTU_MIN, NET_MTU_MIN - 19U), "minimum MTU rejects oversize payload");
    ok &= expect(net_ipv4_payload_fits_mtu(NET_MTU_DEFAULT, NET_MTU_DEFAULT - 20U), "default MTU exact limit");
    ok &= expect(!net_ipv4_payload_fits_mtu(NET_MTU_DEFAULT, NET_MTU_DEFAULT - 19U), "default MTU rejects buffer overflow");
    ok &= expect(!net_ipv4_payload_fits_mtu(0U, 1U), "zero MTU rejected");
    ok &= expect(!net_ipv4_payload_fits_mtu(19U, 1U), "MTU below header size rejected");
    ok &= expect(!net_ipv4_payload_fits_mtu(NET_MTU_MIN - 1U, 1U), "subminimum MTU rejected");
    ok &= expect(!net_ipv4_payload_fits_mtu(NET_MTU_DEFAULT + 1U, 1U), "MTU above fixed buffer rejected");
    ok &= expect(!net_ipv4_payload_fits_mtu(NET_MTU_DEFAULT, 0U), "empty payload rejected");
    ok &= expect(!net_ipv4_payload_fits_mtu(NET_MTU_DEFAULT, SIZE_MAX), "SIZE_MAX payload rejected without overflow");
    if (!ok) return 1;
    puts("IPv4 MTU bounds: PASS");
    return 0;
}
