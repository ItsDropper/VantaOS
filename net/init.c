/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "net.h"
#include "spinlock.h"

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
 *
 * Driver initialization mutates static state and may publish live DMA-backed
 * interfaces. Make this entry point one-shot so a repeated call cannot reset
 * state belonging to an already registered device. Concurrent callers wait
 * for the first initialization to finish.
 */
static spinlock_t network_init_lock = SPINLOCK_INITIALIZER;
static uint32_t network_init_state;

void net_stack_initialize(void)
{
    uint64_t flags = spinlock_lock_irqsave(&network_init_lock);
    if (network_init_state == 2U)
    {
        spinlock_unlock_irqrestore(&network_init_lock, flags);
        return;
    }
    if (network_init_state == 1U)
    {
        spinlock_unlock_irqrestore(&network_init_lock, flags);
        for (;;)
        {
            flags = spinlock_lock_irqsave(&network_init_lock);
            int complete = network_init_state == 2U;
            spinlock_unlock_irqrestore(&network_init_lock, flags);
            if (complete)
                return;
            __asm__ volatile ("pause");
        }
    }
    network_init_state = 1U;
    spinlock_unlock_irqrestore(&network_init_lock, flags);

    net_initialize();

    int protocols_ok = 1;
    if (!net_arp_initialize()) protocols_ok = 0;
    if (!net_ipv4_initialize()) protocols_ok = 0;
    if (!net_ipv6_initialize()) protocols_ok = 0;
    if (!net_udp_initialize()) protocols_ok = 0;
    if (!net_tcp_initialize()) protocols_ok = 0;
    if (!protocols_ok)
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

    flags = spinlock_lock_irqsave(&network_init_lock);
    network_init_state = 2U;
    spinlock_unlock_irqrestore(&network_init_lock, flags);
}
