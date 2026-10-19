/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "net_ipv4.h"
#include "net_ipv6.h"
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
    ok &= expect(net_udp_payload_fits_mtu(NET_MTU_MIN, 0U), "empty UDP datagram fits minimum MTU");
    ok &= expect(net_udp_payload_fits_mtu(NET_MTU_MIN, NET_MTU_MIN - 28U), "UDP minimum MTU exact limit");
    ok &= expect(!net_udp_payload_fits_mtu(NET_MTU_MIN, NET_MTU_MIN - 27U), "UDP payload beyond MTU rejected");
    ok &= expect(net_udp_payload_fits_mtu(NET_MTU_DEFAULT, NET_MTU_DEFAULT - 28U), "UDP default MTU exact limit");
    ok &= expect(!net_udp_payload_fits_mtu(NET_MTU_DEFAULT, NET_MTU_DEFAULT - 27U), "UDP payload cannot overflow segment buffer");
    ok &= expect(!net_udp_payload_fits_mtu(27U, 0U), "UDP MTU smaller than header rejected");
    ok &= expect(!net_udp_payload_fits_mtu(NET_MTU_MIN - 1U, 0U), "UDP subminimum MTU rejected");
    ok &= expect(!net_udp_payload_fits_mtu(NET_MTU_DEFAULT + 1U, 0U), "UDP MTU above fixed buffer rejected");
    ok &= expect(!net_udp_payload_fits_mtu(NET_MTU_DEFAULT, SIZE_MAX), "UDP SIZE_MAX payload rejected without overflow");
    if (!ok) return 1;
    ok &= expect(net_ipv6_payload_fits_mtu(1280U, 1U), "IPv6 minimum MTU accepts payload");
    ok &= expect(net_ipv6_payload_fits_mtu(1280U, 1240U), "IPv6 minimum MTU exact limit");
    ok &= expect(!net_ipv6_payload_fits_mtu(1280U, 1241U), "IPv6 payload beyond minimum MTU rejected");
    ok &= expect(!net_ipv6_payload_fits_mtu(1279U, 1U), "IPv6 MTU below RFC minimum rejected");
    ok &= expect(!net_ipv6_payload_fits_mtu(NET_MTU_DEFAULT + 1U, 1U), "IPv6 MTU above fixed buffer rejected");
    ok &= expect(!net_ipv6_payload_fits_mtu(NET_MTU_DEFAULT, SIZE_MAX), "IPv6 SIZE_MAX payload rejected without overflow");
    if (!ok) return 1;
    puts("IPv4/UDP/IPv6 MTU bounds: PASS");
    return 0;
}
