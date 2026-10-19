/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "net.h"

#include "e1000.h"
#include "virtio_net.h"
#include "virtio_net_modern.h"
#include "rtl8139.h"
#include "net_arp.h"
#include "net_ipv4.h"
#include "net_ipv6.h"
#include "net_tcp.h"
#include "serial.h"
#ifdef VANTAOS_SELF_TEST
#include "net_selftest.h"
#endif

/*
 * Network composition root: initialize the core, register protocols and
 * drivers, then probe PCI only after all network drivers have registered.
 */
void net_stack_initialize(void)
{
    net_initialize();

    if (!net_arp_initialize() || !net_ipv4_initialize() ||
        !net_ipv6_initialize() || !net_udp_initialize() ||
        !net_tcp_initialize())
        serial_write("VANTAOS_NETWORK_PROTOCOL_REGISTER_FAIL\n");

    if (!e1000_initialize())
        serial_write("VANTAOS_E1000_DRIVER_REGISTER_FAIL\n");
    if (!virtio_net_initialize())
        serial_write("VANTAOS_VIRTIO_NET_DRIVER_REGISTER_FAIL\n");
    if (!virtio_net_modern_initialize())
        serial_write("VANTAOS_VIRTIO_NET_MODERN_DRIVER_REGISTER_FAIL\n");
    if (!rtl8139_initialize())
        serial_write("VANTAOS_RTL8139_DRIVER_REGISTER_FAIL\n");

    (void)net_probe_pci_devices();
#ifdef VANTAOS_SELF_TEST
    network_framework_selftest();
#endif
}
