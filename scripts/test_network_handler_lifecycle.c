/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "net.h"
#include "device.h"
#include "net_ipv4.h"

#include <assert.h>

extern int net_internal_dispatch_ethernet(net_device_t*, const uint8_t*,
                                          size_t, uint16_t, size_t);

int net_ipv4_send(net_device_t* device, uint32_t destination,
                  uint8_t protocol, const void* payload, size_t payload_length)
{
    (void)device; (void)destination; (void)protocol;
    (void)payload; (void)payload_length;
    return 0;
}

static net_device_t* active_device;
static uint32_t ethernet_calls;
static uint32_t udp_calls;

static void ethernet_handler(net_device_t* device, const uint8_t* frame,
                             size_t length, uint16_t type, size_t header,
                             void* context)
{
    assert(device == active_device);
    assert(frame != NULL && length >= header);
    assert(type == 0x88B5U);
    assert(context == &ethernet_calls);
    ++ethernet_calls;
    assert(!net_ethernet_handler_unregister(type, ethernet_handler, context));
}

static void udp_handler(net_device_t* device, uint32_t source_address,
                        uint16_t source_port, uint16_t destination_port,
                        const uint8_t* payload, size_t payload_length,
                        void* context)
{
    assert(device == active_device);
    assert(source_address == 0x0A000202U);
    assert(source_port == 1234U && destination_port == 4321U);
    assert(payload_length == 3U);
    assert(payload[0] == 'v' && payload[1] == 'n' && payload[2] == 't');
    assert(context == &udp_calls);
    ++udp_calls;
    assert(!net_udp_handler_unregister(destination_port, udp_handler, context));
}

int main(void)
{
    net_device_t device = {0};
    device.name[0] = 'e'; device.name[1] = 'n'; device.name[2] = '0';
    device.mac[0] = 0x02U;
    device.mtu = NET_MTU_DEFAULT;
    device.link_up = 1U;
    active_device = &device;

    net_initialize();
    assert(net_ethernet_handler_register(0x88B5U, ethernet_handler,
                                         &ethernet_calls));
    uint8_t frame[NET_ETHERNET_HEADER_SIZE] = {0};
    assert(net_internal_dispatch_ethernet(&device, frame, sizeof(frame),
                                          0x88B5U, NET_ETHERNET_HEADER_SIZE));
    assert(ethernet_calls == 1U);
    assert(net_ethernet_handler_unregister(0x88B5U, ethernet_handler,
                                           &ethernet_calls));

    assert(net_udp_handler_register(4321U, udp_handler, &udp_calls));
    uint8_t ip[31] = {0};
    ip[12] = 0x0AU; ip[13] = 0x00U; ip[14] = 0x02U; ip[15] = 0x02U;
    ip[20] = 0x04U; ip[21] = 0xD2U; /* source port 1234 */
    ip[22] = 0x10U; ip[23] = 0xE1U; /* destination port 4321 */
    ip[24] = 0U; ip[25] = 11U;
    ip[28] = 'v'; ip[29] = 'n'; ip[30] = 't';
    net_udp_receive_ipv4(&device, ip, 20U, sizeof(ip));
    assert(udp_calls == 1U);
    assert(net_udp_handler_unregister(4321U, udp_handler, &udp_calls));
    return 0;
}
