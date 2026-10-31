/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "net.h"
#include "net_arp.h"
#include "net_ipv4.h"
#include "net_ipv6.h"
#include "net_tcp.h"

#include <assert.h>

static uint32_t core_calls, arp_calls, ipv4_calls, ipv6_calls, udp_calls;
static uint32_t tcp_calls, e1000_calls, legacy_calls, modern_calls;
static uint32_t rtl_calls, probe_calls;

void net_initialize(void) { ++core_calls; }
int net_arp_initialize(void) { ++arp_calls; return 1; }
int net_ipv4_initialize(void) { ++ipv4_calls; return 1; }
int net_ipv6_initialize(void) { ++ipv6_calls; return 1; }
int net_udp_initialize(void) { ++udp_calls; return 1; }
int net_tcp_initialize(void) { ++tcp_calls; return 1; }
int e1000_initialize(void) { ++e1000_calls; return 1; }
int virtio_net_initialize(void) { ++legacy_calls; return 1; }
int virtio_net_modern_initialize(void) { ++modern_calls; return 1; }
int rtl8139_initialize(void) { ++rtl_calls; return 1; }
uint32_t net_probe_pci_devices(void) { ++probe_calls; return 0U; }
void serial_write(const char* text) { (void)text; }

int main(void)
{
    net_stack_initialize();
    net_stack_initialize();

    assert(core_calls == 1U);
    assert(arp_calls == 1U);
    assert(ipv4_calls == 1U);
    assert(ipv6_calls == 1U);
    assert(udp_calls == 1U);
    assert(tcp_calls == 1U);
    assert(e1000_calls == 1U);
    assert(legacy_calls == 1U);
    assert(modern_calls == 1U);
    assert(rtl_calls == 1U);
    assert(probe_calls == 1U);
    return 0;
}
