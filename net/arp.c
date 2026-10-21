/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "net.h"
#include "serial.h"
#include "net_arp.h"
#include "spinlock.h"
#include "timer.h"
#include <stddef.h>
#include <stdint.h>

#define ARP_ETHERNET_IPV4 0x0800U
#define ARP_REQUEST 1U
#define ARP_REPLY 2U
#define ARP_CACHE_CAPACITY 16U
#define ARP_CACHE_TTL_TICKS 6000U
#define ARP_RETRY_TICKS 100U

typedef struct {
    net_device_t* device;
    uint32_t address;
    uint32_t expires;
    uint8_t mac[6];
    uint8_t valid;
} arp_cache_entry_t;

static arp_cache_entry_t cache[ARP_CACHE_CAPACITY];
static spinlock_t cache_lock = SPINLOCK_INITIALIZER;

static uint16_t read_be16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void write_be16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}

static void write_be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static int valid_sender_ip(uint32_t ip)
{
    uint8_t first = (uint8_t)(ip >> 24);
    return ip != 0U && ip != 0xFFFFFFFFU && first != 0U &&
           first != 127U && first < 224U;
}

static int valid_sender_mac(const uint8_t* mac)
{
    uint8_t any = 0, all_ff = 1;
    for (uint32_t i = 0; i < 6U; ++i) {
        any |= mac[i];
        if (mac[i] != 0xFFU) all_ff = 0;
    }
    return any != 0U && !all_ff && !(mac[0] & 1U);
}

static void cache_learn(net_device_t* device, uint32_t address,
                        const uint8_t mac[6])
{
    uint32_t now = timer_get_ticks();
    uint32_t selected = ARP_CACHE_CAPACITY;
    uint32_t least_remaining = UINT32_MAX;
    uint64_t flags = spinlock_lock_irqsave(&cache_lock);
    for (uint32_t i = 0; i < ARP_CACHE_CAPACITY; ++i) {
        if (cache[i].valid && cache[i].device == device &&
            cache[i].address == address) {
            selected = i;
            break;
        }
        if (!cache[i].valid ||
            (int32_t)(now - cache[i].expires) >= 0) {
            selected = i;
            break;
        }
        uint32_t remaining = cache[i].expires - now;
        if (remaining < least_remaining) {
            least_remaining = remaining;
            selected = i;
        }
    }
    if (selected < ARP_CACHE_CAPACITY) {
        cache[selected].device = device;
        cache[selected].address = address;
        cache[selected].expires = now + ARP_CACHE_TTL_TICKS;
        for (uint32_t i = 0; i < 6U; ++i) cache[selected].mac[i] = mac[i];
        cache[selected].valid = 1U;
    }
    spinlock_unlock_irqrestore(&cache_lock, flags);
}

static int cache_lookup(net_device_t* device, uint32_t address, uint8_t mac[6])
{
    uint32_t now = timer_get_ticks();
    uint64_t flags = spinlock_lock_irqsave(&cache_lock);
    for (uint32_t i = 0; i < ARP_CACHE_CAPACITY; ++i) {
        if (!cache[i].valid) continue;
        if ((int32_t)(now - cache[i].expires) >= 0) {
            cache[i].valid = 0;
            continue;
        }
        if (cache[i].device == device && cache[i].address == address) {
            for (uint32_t j = 0; j < 6U; ++j) mac[j] = cache[i].mac[j];
            spinlock_unlock_irqrestore(&cache_lock, flags);
            return 1;
        }
    }
    spinlock_unlock_irqrestore(&cache_lock, flags);
    return 0;
}

static int send_arp_request(net_device_t* device, uint32_t target_ip)
{
    uint32_t local_ip = 0;
    if (!net_device_get_ipv4(device, &local_ip, 0, 0)) return 0;
    uint8_t frame[NET_ETHERNET_HEADER_SIZE + 28U] = {0};
    for (uint32_t i = 0; i < 6U; ++i) frame[i] = 0xFFU;
    for (uint32_t i = 0; i < 6U; ++i) frame[6U + i] = device->mac[i];
    write_be16(frame + 12U, 0x0806U);
    uint8_t* arp = frame + NET_ETHERNET_HEADER_SIZE;
    write_be16(arp, 1U);
    write_be16(arp + 2U, ARP_ETHERNET_IPV4);
    arp[4] = 6U; arp[5] = 4U;
    write_be16(arp + 6U, ARP_REQUEST);
    for (uint32_t i = 0; i < 6U; ++i) arp[8U + i] = device->mac[i];
    write_be32(arp + 14U, local_ip);
    write_be32(arp + 24U, target_ip);
    return net_device_transmit(device, frame, sizeof(frame));
}

static void arp_receive(net_device_t* device, const uint8_t* frame,
                        size_t length, uint16_t type, size_t header,
                        void* context)
{
    (void)type; (void)context;
    if (!device || !frame ||
        (header != NET_ETHERNET_HEADER_SIZE &&
         header != NET_ETHERNET_VLAN_HEADER_SIZE) ||
        length < header + 28U ||
        read_be16(frame + (header == NET_ETHERNET_VLAN_HEADER_SIZE ? 16U : 12U))
            != 0x0806U)
        return;

    const uint8_t* arp = frame + header;
    uint16_t opcode = read_be16(arp + 6U);
    if (read_be16(arp) != 1U || read_be16(arp + 2U) != ARP_ETHERNET_IPV4 ||
        arp[4] != 6U || arp[5] != 4U ||
        (opcode != ARP_REQUEST && opcode != ARP_REPLY))
        return;

    const uint8_t* sender_mac = arp + 8U;
    uint32_t sender_ip = read_be32(arp + 14U);
    uint32_t target_ip = read_be32(arp + 24U);
    uint32_t local_ip = 0;
    if (!valid_sender_mac(sender_mac) || !valid_sender_ip(sender_ip))
        return;
    for (uint32_t i = 0; i < 6U; ++i)
        if (sender_mac[i] != frame[6U + i]) return;

    if (opcode == ARP_REPLY) {
        if (!net_device_get_ipv4(device, &local_ip, 0, 0) ||
            target_ip != local_ip)
            return;
        for (uint32_t i = 0; i < 6U; ++i)
            if (arp[18U + i] != device->mac[i]) return;
        cache_learn(device, sender_ip, sender_mac);
        return;
    }

    if (!net_device_get_ipv4(device, &local_ip, 0, 0) || target_ip != local_ip)
        return;
    /* Only learn from requests addressed to us; unsolicited ARP is not trusted. */
    cache_learn(device, sender_ip, sender_mac);

    uint8_t out[NET_ETHERNET_VLAN_HEADER_SIZE + 28U] = {0};
    for (uint32_t i = 0; i < 12U; ++i) out[i] = frame[i];
    if (header == NET_ETHERNET_VLAN_HEADER_SIZE) {
        for (uint32_t i = 12U; i < 16U; ++i) out[i] = frame[i];
        write_be16(out + 16U, 0x0806U);
    } else {
        write_be16(out + 12U, 0x0806U);
    }
    for (uint32_t i = 0; i < 6U; ++i) {
        out[i] = sender_mac[i];
        out[6U + i] = device->mac[i];
    }
    uint8_t* reply = out + header;
    write_be16(reply, 1U); write_be16(reply + 2U, ARP_ETHERNET_IPV4);
    reply[4] = 6U; reply[5] = 4U; write_be16(reply + 6U, ARP_REPLY);
    for (uint32_t i = 0; i < 6U; ++i) {
        reply[8U + i] = device->mac[i];
        reply[18U + i] = sender_mac[i];
    }
    write_be32(reply + 14U, local_ip);
    write_be32(reply + 24U, sender_ip);
    int sent = net_device_transmit(device, out, header + 28U);
#ifdef VANTAOS_SELF_TEST
    if (sent) serial_write("VANTAOS_ARP_REPLY_OK\n");
#else
    (void)sent;
#endif
}

int net_arp_initialize(void)
{
    uint64_t flags = spinlock_lock_irqsave(&cache_lock);
    for (uint32_t i = 0; i < ARP_CACHE_CAPACITY; ++i)
        cache[i] = (arp_cache_entry_t){0};
    spinlock_unlock_irqrestore(&cache_lock, flags);
    return net_ethernet_handler_register(0x0806U, arp_receive, 0);
}

int net_arp_resolve(net_device_t* device, uint32_t address,
                    uint8_t mac_out[6], uint32_t timeout_ms)
{
    if (!device || !mac_out || !valid_sender_ip(address) ||
        !device->link_up || timeout_ms < 100U || timeout_ms > 30000U)
        return 0;
    uint32_t local_ip = 0;
    if (!net_device_get_ipv4(device, &local_ip, 0, 0) || address == local_ip)
        return 0;
    if (cache_lookup(device, address, mac_out)) return 1;

    uint32_t start = timer_get_ticks();
    uint32_t timeout_ticks = timeout_ms / 10U;
    uint32_t last_request = start - ARP_RETRY_TICKS;
    while ((uint32_t)(timer_get_ticks() - start) < timeout_ticks) {
        uint32_t now = timer_get_ticks();
        if ((uint32_t)(now - last_request) >= ARP_RETRY_TICKS) {
            (void)send_arp_request(device, address);
            last_request = now;
        }
        (void)net_poll(16U);
        if (cache_lookup(device, address, mac_out)) return 1;
        __asm__ volatile ("pause");
    }
    return cache_lookup(device, address, mac_out);
}
