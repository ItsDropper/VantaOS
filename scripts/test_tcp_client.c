#include "net_tcp.h"
#include "net_ipv4.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LOCAL_IP 0x0A00020FU
#define REMOTE_IP 0x0A000202U
#define TCP_ACK 0x10U
#define TCP_SYN 0x02U
#define TCP_FIN 0x01U
#define TCP_PSH 0x08U

typedef struct {
    uint8_t bytes[1600];
    size_t length;
} queued_packet_t;

static net_device_t device;
static queued_packet_t queue[8];
static size_t queue_head, queue_used;
static uint32_t ticks;
static uint32_t remote_sequence = 1000U;
static const char response[] =
    "HTTP/1.0 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello";

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void write16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void write32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t sum_bytes(uint32_t sum, const uint8_t *p, size_t length)
{
    while (length >= 2U) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        length -= 2U;
    }
    if (length) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return sum;
}

static uint16_t pseudo_checksum(uint32_t source, uint32_t destination,
                                const uint8_t *tcp, size_t length)
{
    uint8_t pseudo[12];
    write32(pseudo, source);
    write32(pseudo + 4U, destination);
    pseudo[8] = 0U;
    pseudo[9] = 6U;
    write16(pseudo + 10U, (uint16_t)length);
    uint32_t sum = sum_bytes(0U, pseudo, sizeof(pseudo));
    sum = sum_bytes(sum, tcp, length);
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}

static int enqueue_tcp(uint16_t source_port, uint16_t destination_port,
                       uint32_t sequence, uint32_t acknowledgement,
                       uint8_t flags, const void *payload, size_t payload_length)
{
    if (queue_used == sizeof(queue) / sizeof(queue[0]) ||
        payload_length + 40U > sizeof(queue[0].bytes))
        return 0;
    size_t tcp_length = 20U + payload_length;
    queued_packet_t *item = &queue[(queue_head + queue_used) %
                                   (sizeof(queue) / sizeof(queue[0]))];
    uint8_t *ip = item->bytes;
    uint8_t *tcp = ip + 20U;
    for (size_t i = 0; i < 20U; ++i) ip[i] = 0U;
    for (size_t i = 0; i < tcp_length; ++i) tcp[i] = 0U;
    ip[0] = 0x45U;
    write16(ip + 2U, (uint16_t)(20U + tcp_length));
    ip[8] = 64U;
    ip[9] = 6U;
    write32(ip + 12U, REMOTE_IP);
    write32(ip + 16U, LOCAL_IP);
    write16(tcp, source_port);
    write16(tcp + 2U, destination_port);
    write32(tcp + 4U, sequence);
    write32(tcp + 8U, acknowledgement);
    tcp[12] = 0x50U;
    tcp[13] = flags;
    write16(tcp + 14U, 8192U);
    const uint8_t *bytes = (const uint8_t *)payload;
    for (size_t i = 0; i < payload_length; ++i) tcp[20U + i] = bytes[i];
    write16(tcp + 16U, pseudo_checksum(REMOTE_IP, LOCAL_IP, tcp, tcp_length));
    item->length = 20U + tcp_length;
    ++queue_used;
    return 1;
}

uint32_t timer_get_ticks(void)
{
    return ticks++;
}

int net_device_get_ipv4(net_device_t *d, uint32_t *address,
                        uint32_t *mask, uint32_t *gateway)
{
    if (d != &device || !device.ipv4_configured) return 0;
    if (address) *address = LOCAL_IP;
    if (mask) *mask = 0xFFFFFF00U;
    if (gateway) *gateway = 0x0A000202U;
    return 1;
}

size_t net_poll(size_t budget)
{
    if (!budget || !queue_used) {
        ++ticks;
        return 0U;
    }
    queued_packet_t *item = &queue[queue_head];
    uint16_t total = read16(item->bytes + 2U);
    net_tcp_receive_ipv4(&device, item->bytes, 20U, total);
    queue_head = (queue_head + 1U) % (sizeof(queue) / sizeof(queue[0]));
    --queue_used;
    ++ticks;
    return 1U;
}

int net_ipv4_send(net_device_t *d, uint32_t destination, uint8_t protocol,
                  const void *payload, size_t payload_length)
{
    if (d != &device || destination != REMOTE_IP || protocol != 6U ||
        !payload || payload_length < 20U)
        return 0;
    const uint8_t *tcp = (const uint8_t *)payload;
    uint16_t source_port = read16(tcp);
    uint16_t destination_port = read16(tcp + 2U);
    uint32_t sequence = read32(tcp + 4U);
    uint8_t flags = tcp[13];
    size_t header_length = (size_t)(tcp[12] >> 4) * 4U;
    if (header_length < 20U || header_length > payload_length ||
        pseudo_checksum(LOCAL_IP, REMOTE_IP, tcp, payload_length) != 0U)
        return 0;

    if ((flags & (TCP_SYN | TCP_ACK)) == TCP_SYN) {
        remote_sequence = 1000U;
        return enqueue_tcp(destination_port, source_port, remote_sequence,
                           sequence + 1U, TCP_SYN | TCP_ACK, 0, 0U);
    }
    if ((flags & (TCP_ACK | TCP_PSH)) == (TCP_ACK | TCP_PSH)) {
        size_t data_length = payload_length - header_length;
        /*
         * First inject a segment whose ACK claims bytes the client has not
         * sent. Its payload must not be accepted even though its receive
         * sequence is otherwise correct. This catches ACK-validation
         * regressions in the real TCP implementation.
         */
        static const char invalid_payload[] = "BAD";
        if (!enqueue_tcp(destination_port, source_port, remote_sequence,
                         sequence + (uint32_t)data_length + 100U,
                         TCP_ACK | TCP_PSH, invalid_payload,
                         sizeof(invalid_payload) - 1U))
            return 0;
        if (!enqueue_tcp(destination_port, source_port, remote_sequence,
                         sequence + (uint32_t)data_length, TCP_ACK, 0, 0U))
            return 0;
        remote_sequence++;
        if (!enqueue_tcp(destination_port, source_port, remote_sequence,
                         sequence + (uint32_t)data_length,
                         TCP_ACK | TCP_PSH | TCP_FIN,
                         response, sizeof(response) - 1U))
            return 0;
        remote_sequence += (uint32_t)sizeof(response);
        return 1;
    }
    if (flags & TCP_FIN)
        return enqueue_tcp(destination_port, source_port, remote_sequence,
                           sequence + 1U, TCP_ACK, 0, 0U);
    return 1;
}

int main(void)
{
    static const char request[] =
        "GET / HTTP/1.0\r\nHost: test.local\r\nConnection: close\r\n\r\n";
    uint8_t received[256];
    device.link_up = 1U;
    device.ipv4_configured = 1U;
    device.mtu = 1500U;
    if (!net_tcp_initialize()) return 1;
    if (!net_tcp_connect(&device, REMOTE_IP, 80U, 3000U)) {
        fputs("TCP handshake failed\n", stderr);
        return 2;
    }
    if (!net_tcp_send(&device, request, sizeof(request) - 1U, 3000U)) {
        fputs("TCP request/ACK failed\n", stderr);
        return 3;
    }
    int n = net_tcp_receive(&device, received, sizeof(received) - 1U, 3000U);
    if (n != (int)(sizeof(response) - 1U)) {
        fputs("TCP response length mismatch\n", stderr);
        return 4;
    }
    received[n] = 0;
    if (strcmp((const char *)received, response) != 0) {
        fputs("TCP response payload mismatch\n", stderr);
        return 5;
    }
    if (net_tcp_receive(&device, received, sizeof(received), 3000U) != 0) {
        fputs("TCP orderly EOF missing\n", stderr);
        return 6;
    }
    net_tcp_close(&device);
    puts("TCP client handshake, checksum, send/ACK, receive/FIN, and close passed");
    return 0;
}
