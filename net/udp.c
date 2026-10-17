#include "net.h"
#include "net_ipv4.h"
#include "serial.h"
#include "spinlock.h"
#include <stddef.h>
#include <stdint.h>

#define IPV4_PROTOCOL_UDP 17U
#define UDP_HEADER_SIZE 8U
#define UDP_SELFTEST_PORT 4242U
#define UDP_SELFTEST_PAYLOAD "VANTAOS-UDP"

typedef struct {
    uint16_t port;
    net_udp_handler_fn fn;
    void* context;
} udp_handler_t;

static udp_handler_t handlers[NET_UDP_HANDLER_MAX];
static uint32_t handlers_used;
static spinlock_t handler_lock = SPINLOCK_INITIALIZER;

static uint16_t read_be16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t checksum_add(uint32_t sum, const uint8_t* p, size_t length)
{
    while (length >= 2U) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        length -= 2U;
    }
    if (length != 0U)
        sum += (uint32_t)p[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFU) + (sum >> 16);
    return sum;
}

static int udp_checksum_valid(const uint8_t* ip, const uint8_t* udp,
                              size_t udp_length)
{
    /* A zero UDP checksum is legal for IPv4 and means checksum disabled. */
    if (read_be16(udp + 6U) == 0U)
        return 1;

    uint32_t sum = 0;
    sum = checksum_add(sum, ip + 12U, 8U);
    sum += IPV4_PROTOCOL_UDP;
    sum += (uint16_t)udp_length;
    sum = checksum_add(sum, udp, udp_length);
    while (sum >> 16)
        sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)sum == 0xFFFFU;
}

int net_udp_handler_register(uint16_t port, net_udp_handler_fn fn, void* context)
{
    if (port == 0U || !fn)
        return 0;

    uint64_t flags = spinlock_lock_irqsave(&handler_lock);
    for (uint32_t i = 0; i < handlers_used; ++i) {
        if (handlers[i].port == port) {
            spinlock_unlock_irqrestore(&handler_lock, flags);
            return 0;
        }
    }
    if (handlers_used == NET_UDP_HANDLER_MAX) {
        spinlock_unlock_irqrestore(&handler_lock, flags);
        return 0;
    }
    handlers[handlers_used++] = (udp_handler_t){port, fn, context};
    spinlock_unlock_irqrestore(&handler_lock, flags);
    return 1;
}

int net_udp_handler_unregister(uint16_t port, net_udp_handler_fn fn, void* context)
{
    uint64_t flags = spinlock_lock_irqsave(&handler_lock);
    uint32_t at = handlers_used;
    for (uint32_t i = 0; i < handlers_used; ++i) {
        if (handlers[i].port == port && handlers[i].fn == fn &&
            handlers[i].context == context) {
            at = i;
            break;
        }
    }
    if (at == handlers_used) {
        spinlock_unlock_irqrestore(&handler_lock, flags);
        return 0;
    }
    for (uint32_t i = at; i + 1U < handlers_used; ++i)
        handlers[i] = handlers[i + 1U];
    --handlers_used;
    spinlock_unlock_irqrestore(&handler_lock, flags);
    return 1;
}

void net_udp_receive_ipv4(net_device_t* device, const uint8_t* ip,
                          size_t ip_header_length, size_t ip_total_length)
{
    if (!device || !ip || ip_header_length < 20U || ip_header_length > 60U ||
        ip_total_length < ip_header_length + UDP_HEADER_SIZE)
        return;

    const uint8_t* udp = ip + ip_header_length;
    size_t ip_payload_length = ip_total_length - ip_header_length;
    size_t udp_length = read_be16(udp + 4U);
    uint16_t source_port = read_be16(udp);
    uint16_t destination_port = read_be16(udp + 2U);
    if (udp_length < UDP_HEADER_SIZE || udp_length != ip_payload_length ||
        source_port == 0U || destination_port == 0U ||
        !udp_checksum_valid(ip, udp, udp_length))
        return;

    udp_handler_t snapshot;
    int found = 0;
    uint64_t flags = spinlock_lock_irqsave(&handler_lock);
    for (uint32_t i = 0; i < handlers_used; ++i) {
        if (handlers[i].port == destination_port) {
            snapshot = handlers[i];
            found = 1;
            break;
        }
    }
    spinlock_unlock_irqrestore(&handler_lock, flags);
    if (!found)
        return;

    snapshot.fn(device, read_be32(ip + 12U), source_port, destination_port,
                udp + UDP_HEADER_SIZE, udp_length - UDP_HEADER_SIZE,
                snapshot.context);
}


int net_udp_send(net_device_t* device, uint32_t destination_address,
                 uint16_t source_port, uint16_t destination_port,
                 const void* payload, size_t payload_length)
{
    if (!device || !payload || source_port == 0U || destination_port == 0U ||
        payload_length > (size_t)device->mtu - 28U ||
        payload_length > 0xFFFFU - UDP_HEADER_SIZE)
        return 0;

    uint32_t source_address = 0;
    if (!net_device_get_ipv4(device, &source_address, 0, 0))
        return 0;

    size_t udp_length = UDP_HEADER_SIZE + payload_length;
    uint8_t segment[NET_MTU_DEFAULT] = {0};
    put16(segment, source_port);
    put16(segment + 2U, destination_port);
    put16(segment + 4U, (uint16_t)udp_length);
    const uint8_t* bytes = (const uint8_t*)payload;
    for (size_t i = 0; i < payload_length; ++i)
        segment[UDP_HEADER_SIZE + i] = bytes[i];

    uint8_t pseudo[12];
    put32(pseudo, source_address);
    put32(pseudo + 4U, destination_address);
    pseudo[8] = 0U;
    pseudo[9] = IPV4_PROTOCOL_UDP;
    put16(pseudo + 10U, (uint16_t)udp_length);
    uint32_t sum = checksum_add(0U, pseudo, sizeof(pseudo));
    sum += IPV4_PROTOCOL_UDP;
    sum += (uint16_t)udp_length;
    sum = checksum_add(sum, segment, udp_length);
    while (sum >> 16)
        sum = (sum & 0xFFFFU) + (sum >> 16);
    uint16_t value = (uint16_t)~sum;
    if (value == 0U) value = 0xFFFFU;
    put16(segment + 6U, value);

    return net_ipv4_send(device, destination_address, IPV4_PROTOCOL_UDP,
                         segment, udp_length);
}

#ifdef VANTAOS_SELF_TEST
static void udp_selftest_handler(net_device_t* device, uint32_t source_address,
                                 uint16_t source_port, uint16_t destination_port,
                                 const uint8_t* payload, size_t payload_length,
                                 void* context)
{
    (void)device;
    (void)source_address;
    (void)source_port;
    (void)destination_port;
    (void)context;
    static const uint8_t expected[] = UDP_SELFTEST_PAYLOAD;
    if (payload && payload_length == sizeof(expected) - 1U) {
        size_t i = 0;
        while (i < payload_length && payload[i] == expected[i])
            ++i;
        if (i == payload_length)
            serial_write("VANTAOS_UDP_RX_SELFTEST_OK\n");
    }
}
#endif

int net_udp_initialize(void)
{
#ifdef VANTAOS_SELF_TEST
    return net_udp_handler_register(UDP_SELFTEST_PORT, udp_selftest_handler, 0);
#else
    return 1;
#endif
}
