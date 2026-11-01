/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "net.h"
#include "device.h"

#include <assert.h>

extern int net_internal_pop_rx(net_device_t**, uint8_t*, size_t, size_t*);
extern void net_internal_release_rx_device(net_device_t*);

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
    net_device_t other_network = {
        .name = "en-other",
        .mac = {0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U},
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
    assert(net_device_register(&other_network));
    active_network = &network;
    net_initialize(); /* Must not reset an already-published interface. */
    assert(net_device_count() == 2U);
    assert(net_device_transmit(&network, frame, sizeof(frame)));
    assert(transmit_calls == 1U);
    assert(network.core.state == DEVICE_STATE_REGISTERED);
    net_device_stats_t stats;
    assert(net_device_get_stats(&network, &stats));
    assert(stats.tx_frames == 1U);
    assert(stats.tx_bytes == sizeof(frame));
    assert(stats.tx_errors == 0U);
    assert(net_device_count() == 2U);
    assert(device_count() == 3U);
    assert(network.core.state == DEVICE_STATE_REGISTERED);
    assert(network.core.bus == DEVICE_BUS_VIRTUAL);
    assert(network.core.parent == &pci_parent);
    assert(network.core.bus_data == &network);
    assert(network.core.owner == &network);
    assert(!device_unregister(&network.core));
    assert(network.core.vendor_id == 0U);
    assert(network.core.device_id == 0U);
    assert(network.core.class_code == 0U);
    assert(device_find(DEVICE_BUS_VIRTUAL, "en-test") == &network.core);
    assert(!device_unregister(&pci_parent));

    /*
     * Unregistering an interface drops only its queued frames and preserves
     * FIFO order for packets queued on other live interfaces.
     */
    assert(net_receive_frame(&network, frame, sizeof(frame)));
    assert(net_receive_frame(&other_network, frame, sizeof(frame)));
    assert(net_device_unregister(&network));
    assert(network.core.state == DEVICE_STATE_UNREGISTERED);
    assert(net_device_count() == 1U);
    assert(device_count() == 2U);
    net_device_t* queued_device = NULL;
    uint8_t queued_frame[NET_FRAME_MAX_SIZE];
    size_t queued_length = 0U;
    assert(net_internal_pop_rx(&queued_device, queued_frame,
                               sizeof(queued_frame), &queued_length));
    assert(queued_device == &other_network);
    assert(queued_length == sizeof(frame));
    net_internal_release_rx_device(queued_device);
    assert(!net_internal_pop_rx(&queued_device, queued_frame,
                                sizeof(queued_frame), &queued_length));
    assert(net_device_unregister(&other_network));
    assert(net_device_count() == 0U);
    assert(device_count() == 1U);
    assert(device_unregister(&pci_parent));
    assert(device_count() == 0U);
    return 0;
}
