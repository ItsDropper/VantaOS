/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "net.h"
#include "device.h"

#include <assert.h>

static net_device_t* active_network;
static uint32_t transmit_calls;

static int transmit(net_device_t* device, const uint8_t* frame, size_t length)
{
    assert(device == active_network);
    ++transmit_calls;
    /* Teardown from a concurrent path must not race an active TX callback. */
    assert(!net_device_unregister(device));
    return frame != NULL && length >= NET_ETHERNET_HEADER_SIZE;
}

static const net_device_ops_t operations = { .transmit = transmit };

int main(void)
{
    device_t pci_parent = {
        .name = "pci0000:00:02.0",
        .bus = DEVICE_BUS_PCI
    };
    net_device_t network = {
        .name = "en-test",
        .mac = {0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U},
        .mtu = NET_MTU_DEFAULT,
        .link_up = 1U,
        .ops = &operations,
        .parent_device = &pci_parent
    };

    uint8_t frame[NET_ETHERNET_HEADER_SIZE] = {0};
    frame[12] = 0x08U;
    frame[13] = 0x00U;

    assert(device_register(&pci_parent));
    net_initialize();
    assert(net_device_register(&network));
    active_network = &network;
    net_initialize(); /* Must not reset an already-published interface. */
    assert(net_device_count() == 1U);
    assert(net_device_transmit(&network, frame, sizeof(frame)));
    assert(transmit_calls == 1U);
    assert(network.core.state == DEVICE_STATE_REGISTERED);
    net_device_stats_t stats;
    assert(net_device_get_stats(&network, &stats));
    assert(stats.tx_frames == 1U);
    assert(stats.tx_bytes == sizeof(frame));
    assert(stats.tx_errors == 0U);
    assert(net_device_count() == 1U);
    assert(device_count() == 2U);
    assert(network.core.state == DEVICE_STATE_REGISTERED);
    assert(network.core.bus == DEVICE_BUS_VIRTUAL);
    assert(network.core.parent == &pci_parent);
    assert(network.core.bus_data == &network);
    assert(network.core.vendor_id == 0U);
    assert(network.core.device_id == 0U);
    assert(network.core.class_code == 0U);
    assert(device_find(DEVICE_BUS_VIRTUAL, "en-test") == &network.core);
    assert(!device_unregister(&pci_parent));

    assert(net_device_unregister(&network));
    assert(network.core.state == DEVICE_STATE_UNREGISTERED);
    assert(net_device_count() == 0U);
    assert(device_count() == 1U);
    assert(device_unregister(&pci_parent));
    assert(device_count() == 0U);
    return 0;
}
