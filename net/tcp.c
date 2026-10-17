#include "net_tcp.h"
#include "net_ipv4.h"
#include "timer.h"
#include <stddef.h>
#include <stdint.h>

#define TCP_PROTOCOL 6U
#define TCP_FLAG_FIN 0x01U
#define TCP_FLAG_SYN 0x02U
#define TCP_FLAG_RST 0x04U
#define TCP_FLAG_PSH 0x08U
#define TCP_FLAG_ACK 0x10U
#define TCP_HEADER_SIZE 20U
#define TCP_SYN_HEADER_SIZE 24U
#define TCP_DEFAULT_MSS 536U
#define TCP_MAX_MSS 1460U
#define TCP_RETRY_TICKS 100U
#define TCP_MAX_RETRIES 4U

enum { TCP_CLOSED = 0, TCP_SYN_SENT = 1, TCP_ESTABLISHED = 2, TCP_CLOSING = 3 };

typedef struct {
    net_device_t *device;
    uint32_t remote_ip;
    uint32_t snd_una, snd_nxt, rcv_nxt;
    uint16_t local_port, remote_port, peer_mss, peer_window;
    uint8_t state, remote_fin, failed;
    uint8_t rx[NET_TCP_RX_CAPACITY];
    size_t rx_head, rx_used;
} tcp_client_t;

static tcp_client_t client;
static uint32_t next_local_port = 49152U;

static void reset_client(void)
{
    client.device = 0;
    client.remote_ip = 0U;
    client.snd_una = 0U;
    client.snd_nxt = 0U;
    client.rcv_nxt = 0U;
    client.local_port = 0U;
    client.remote_port = 0U;
    client.peer_mss = TCP_DEFAULT_MSS;
    client.peer_window = 0U;
    client.state = TCP_CLOSED;
    client.remote_fin = 0U;
    client.failed = 0U;
    client.rx_head = 0U;
    client.rx_used = 0U;
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t checksum_add(uint32_t sum, const uint8_t *p, size_t length)
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

static uint16_t tcp_checksum(uint32_t source, uint32_t destination,
                             const uint8_t *segment, size_t length)
{
    uint8_t pseudo[12];
    put32(pseudo, source);
    put32(pseudo + 4U, destination);
    pseudo[8] = 0U;
    pseudo[9] = TCP_PROTOCOL;
    put16(pseudo + 10U, (uint16_t)length);
    uint32_t sum = checksum_add(0U, pseudo, sizeof(pseudo));
    sum = checksum_add(sum, segment, length);
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}

static int tcp_checksum_valid(uint32_t source, uint32_t destination,
                              const uint8_t *segment, size_t length)
{
    uint8_t pseudo[12];
    put32(pseudo, source);
    put32(pseudo + 4U, destination);
    pseudo[8] = 0U;
    pseudo[9] = TCP_PROTOCOL;
    put16(pseudo + 10U, (uint16_t)length);
    uint32_t sum = checksum_add(0U, pseudo, sizeof(pseudo));
    sum = checksum_add(sum, segment, length);
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)sum == 0xFFFFU;
}

static int emit_segment(net_device_t *device, uint32_t destination,
                        uint16_t source_port, uint16_t destination_port,
                        uint32_t sequence, uint32_t acknowledgement,
                        uint8_t flags, const void *payload, size_t payload_length)
{
    if (!device || payload_length > TCP_MAX_MSS ||
        payload_length + TCP_HEADER_SIZE > (size_t)device->mtu - 20U)
        return 0;

    uint32_t source = 0;
    if (!net_device_get_ipv4(device, &source, 0, 0)) return 0;
    size_t header_length = (flags & TCP_FLAG_SYN) ? TCP_SYN_HEADER_SIZE : TCP_HEADER_SIZE;
    size_t length = header_length + payload_length;
    uint8_t segment[NET_MTU_DEFAULT];
    put16(segment, source_port);
    put16(segment + 2U, destination_port);
    put32(segment + 4U, sequence);
    put32(segment + 8U, acknowledgement);
    segment[12] = (uint8_t)((header_length / 4U) << 4);
    segment[13] = flags;
    segment[16] = 0U; segment[17] = 0U;
    segment[18] = 0U; segment[19] = 0U;
    put16(segment + 14U, (uint16_t)(NET_TCP_RX_CAPACITY -
        (client.rx_used < NET_TCP_RX_CAPACITY ? client.rx_used : NET_TCP_RX_CAPACITY)));
    if (header_length == TCP_SYN_HEADER_SIZE) {
        segment[20] = 2U; /* MSS option */
        segment[21] = 4U;
        put16(segment + 22U, TCP_MAX_MSS);
    }
    const uint8_t *bytes = (const uint8_t *)payload;
    for (size_t i = 0; i < payload_length; ++i)
        segment[header_length + i] = bytes[i];
    put16(segment + 16U, tcp_checksum(source, destination, segment, length));
    return net_ipv4_send(device, destination, TCP_PROTOCOL, segment, length);
}

static void send_ack(void)
{
    if (client.device && client.state != TCP_CLOSED)
        (void)emit_segment(client.device, client.remote_ip, client.local_port,
                           client.remote_port, client.snd_nxt, client.rcv_nxt,
                           TCP_FLAG_ACK, 0, 0U);
}

int net_tcp_initialize(void)
{
    reset_client();
    client.peer_mss = TCP_DEFAULT_MSS;
    return 1;
}

void net_tcp_receive_ipv4(net_device_t *device, const uint8_t *ip,
                          size_t ip_header_length, size_t ip_total_length)
{
    if (!device || !ip || ip_header_length < 20U || ip_header_length > 60U ||
        ip_total_length < ip_header_length + TCP_HEADER_SIZE ||
        ip[9] != TCP_PROTOCOL)
        return;

    uint32_t source_ip = be32(ip + 12U);
    uint32_t destination_ip = be32(ip + 16U);
    uint32_t local_ip = 0;
    if (!net_device_get_ipv4(device, &local_ip, 0, 0) ||
        destination_ip != local_ip)
        return;

    const uint8_t *tcp = ip + ip_header_length;
    size_t tcp_length = ip_total_length - ip_header_length;
    size_t header_length = (size_t)(tcp[12] >> 4) * 4U;
    if (header_length < TCP_HEADER_SIZE || header_length > 60U ||
        header_length > tcp_length ||
        !tcp_checksum_valid(source_ip, destination_ip, tcp, tcp_length))
        return;

    uint16_t source_port = be16(tcp);
    uint16_t destination_port = be16(tcp + 2U);
    uint32_t sequence = be32(tcp + 4U);
    uint32_t acknowledgement = be32(tcp + 8U);
    uint8_t flags = tcp[13];
    uint16_t advertised_window = be16(tcp + 14U);
    const uint8_t *payload = tcp + header_length;
    size_t payload_length = tcp_length - header_length;

    if (client.state == TCP_CLOSED || device != client.device ||
        source_ip != client.remote_ip || source_port != client.remote_port ||
        destination_port != client.local_port)
        return;

    if (flags & TCP_FLAG_RST) {
        client.failed = 1U;
        client.state = TCP_CLOSED;
        return;
    }

    if (client.state == TCP_SYN_SENT) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) !=
                (TCP_FLAG_SYN | TCP_FLAG_ACK) ||
            acknowledgement != client.snd_nxt)
            return;
        client.rcv_nxt = sequence + 1U;
        client.snd_una = acknowledgement;
        client.peer_window = advertised_window;
        client.state = TCP_ESTABLISHED;
        client.peer_mss = TCP_DEFAULT_MSS;
        int mss_seen = 0;
        for (size_t at = TCP_HEADER_SIZE; at + 1U < header_length;) {
            uint8_t kind = tcp[at];
            if (kind == 0U) break;
            if (kind == 1U) { ++at; continue; }
            if (at + 2U > header_length || tcp[at + 1U] < 2U ||
                (size_t)tcp[at + 1U] > header_length - at)
                break;
            uint8_t option_length = tcp[at + 1U];
            if (kind == 2U && option_length == 4U) {
                uint16_t mss = be16(tcp + at + 2U);
                if (mss >= 64U) {
                    client.peer_mss = mss < TCP_MAX_MSS ? mss : TCP_MAX_MSS;
                    mss_seen = 1;
                }
            }
            at += option_length;
        }
        if (!mss_seen) client.peer_mss = TCP_DEFAULT_MSS;
        send_ack();
        return;
    }

    if (client.state != TCP_ESTABLISHED && client.state != TCP_CLOSING)
        return;

    if (flags & TCP_FLAG_ACK) {
        if ((int32_t)(acknowledgement - client.snd_una) >= 0 &&
            (int32_t)(client.snd_nxt - acknowledgement) >= 0)
            client.snd_una = acknowledgement;
        client.peer_window = advertised_window;
    }

    if (payload_length && sequence == client.rcv_nxt) {
        size_t free_space = NET_TCP_RX_CAPACITY - client.rx_used;
        size_t copy = payload_length < free_space ? payload_length : free_space;
        size_t tail = (client.rx_head + client.rx_used) % NET_TCP_RX_CAPACITY;
        for (size_t i = 0; i < copy; ++i)
            client.rx[(tail + i) % NET_TCP_RX_CAPACITY] = payload[i];
        client.rx_used += copy;
        client.rcv_nxt += (uint32_t)copy;
        if (copy != payload_length) {
            send_ack();
            return;
        }
    } else if (payload_length && sequence != client.rcv_nxt) {
        send_ack();
        return;
    }

    if (flags & TCP_FLAG_FIN) {
        uint32_t fin_sequence = sequence + (uint32_t)payload_length;
        if (fin_sequence == client.rcv_nxt) {
            ++client.rcv_nxt;
            client.remote_fin = 1U;
        }
        send_ack();
    } else if (payload_length) {
        send_ack();
    }
}

int net_tcp_connect(net_device_t *device, uint32_t destination,
                    uint16_t destination_port, uint32_t timeout_ms)
{
    if (!device || !device->link_up || !device->ipv4_configured ||
        !destination || !destination_port || timeout_ms < 1000U ||
        timeout_ms > 120000U || client.state != TCP_CLOSED)
        return 0;

    reset_client();
    client.device = device;
    client.remote_ip = destination;
    client.remote_port = destination_port;
    client.local_port = (uint16_t)next_local_port++;
    if (next_local_port > 65535U) next_local_port = 49152U;
    client.peer_mss = TCP_MAX_MSS;
    uint32_t initial = (timer_get_ticks() * 1103515245U) ^
                       destination ^ ((uint32_t)destination_port << 16);
    client.snd_una = initial;
    client.snd_nxt = initial + 1U;
    client.state = TCP_SYN_SENT;

    if (!emit_segment(device, destination, client.local_port, destination_port,
                      initial, 0U, TCP_FLAG_SYN, 0, 0U)) {
        reset_client();
        return 0;
    }

    uint32_t start = timer_get_ticks();
    uint32_t limit = timeout_ms / 10U;
    uint32_t last = start;
    uint32_t retries = 0U;
    while (client.state == TCP_SYN_SENT &&
           (uint32_t)(timer_get_ticks() - start) < limit) {
        (void)net_poll(16U);
        uint32_t now = timer_get_ticks();
        if (client.state == TCP_ESTABLISHED) return 1;
        if ((uint32_t)(now - last) >= TCP_RETRY_TICKS) {
            if (retries++ >= TCP_MAX_RETRIES) break;
            (void)emit_segment(device, destination, client.local_port,
                               destination_port, initial, 0U, TCP_FLAG_SYN, 0, 0U);
            last = now;
        }
        __asm__ volatile("pause");
    }
    reset_client();
    return 0;
}

int net_tcp_send(net_device_t *device, const void *data, size_t length,
                 uint32_t timeout_ms)
{
    if (!device || device != client.device || client.state != TCP_ESTABLISHED ||
        !data || !length || timeout_ms < 100U || timeout_ms > 30000U)
        return 0;

    const uint8_t *bytes = (const uint8_t *)data;
    size_t maximum = client.peer_mss;
    if (maximum > (size_t)device->mtu - 40U)
        maximum = (size_t)device->mtu - 40U;
    if (!maximum) return 0;

    size_t sent = 0U;
    while (sent < length) {
        uint32_t start = timer_get_ticks();
        uint32_t limit = timeout_ms / 10U;
        while (!client.peer_window && client.state == TCP_ESTABLISHED &&
               (uint32_t)(timer_get_ticks() - start) < limit) {
            (void)net_poll(16U);
            __asm__ volatile("pause");
        }
        if (client.state != TCP_ESTABLISHED || !client.peer_window) return 0;

        size_t chunk = length - sent;
        if (chunk > maximum) chunk = maximum;
        if (chunk > client.peer_window) chunk = client.peer_window;
        uint32_t sequence = client.snd_nxt;
        uint32_t target = sequence + (uint32_t)chunk;
        if (!emit_segment(device, client.remote_ip, client.local_port,
                          client.remote_port, sequence, client.rcv_nxt,
                          TCP_FLAG_ACK | TCP_FLAG_PSH, bytes + sent, chunk))
            return 0;
        client.snd_nxt = target;

        start = timer_get_ticks();
        uint32_t last = start, retries = 0U;
        while ((int32_t)(client.snd_una - target) < 0 &&
               client.state == TCP_ESTABLISHED &&
               (uint32_t)(timer_get_ticks() - start) < limit) {
            (void)net_poll(16U);
            uint32_t now = timer_get_ticks();
            if ((uint32_t)(now - last) >= TCP_RETRY_TICKS) {
                if (retries++ >= TCP_MAX_RETRIES) break;
                (void)emit_segment(device, client.remote_ip, client.local_port,
                    client.remote_port, sequence, client.rcv_nxt,
                    TCP_FLAG_ACK | TCP_FLAG_PSH, bytes + sent, chunk);
                last = now;
            }
            __asm__ volatile("pause");
        }
        if (client.state != TCP_ESTABLISHED ||
            (int32_t)(client.snd_una - target) < 0) return 0;
        sent += chunk;
    }
    return 1;
}

int net_tcp_receive(net_device_t *device, void *buffer, size_t capacity,
                    uint32_t timeout_ms)
{
    if (!device || device != client.device || !buffer || !capacity ||
        (client.state != TCP_ESTABLISHED && client.state != TCP_CLOSING) ||
        timeout_ms < 100U || timeout_ms > 120000U)
        return -1;

    uint32_t start = timer_get_ticks();
    uint32_t limit = timeout_ms / 10U;
    while (!client.rx_used && !client.remote_fin && !client.failed &&
           client.state != TCP_CLOSED &&
           (uint32_t)(timer_get_ticks() - start) < limit) {
        (void)net_poll(16U);
        __asm__ volatile("pause");
    }
    if (client.failed || client.state == TCP_CLOSED) return -1;
    if (!client.rx_used && client.remote_fin) return 0;
    if (!client.rx_used) return -1;

    size_t amount = client.rx_used < capacity ? client.rx_used : capacity;
    uint8_t *out = (uint8_t *)buffer;
    for (size_t i = 0; i < amount; ++i)
        out[i] = client.rx[(client.rx_head + i) % NET_TCP_RX_CAPACITY];
    client.rx_head = (client.rx_head + amount) % NET_TCP_RX_CAPACITY;
    client.rx_used -= amount;
    send_ack();
    return (int)amount;
}

void net_tcp_close(net_device_t *device)
{
    if (!device || device != client.device) return;
    if (client.state == TCP_ESTABLISHED) {
        uint32_t sequence = client.snd_nxt;
        if (emit_segment(device, client.remote_ip, client.local_port,
                        client.remote_port, sequence, client.rcv_nxt,
                        TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0U)) {
            client.snd_nxt++;
            client.state = TCP_CLOSING;
            uint32_t start = timer_get_ticks();
            while ((int32_t)(client.snd_una - client.snd_nxt) < 0 &&
                   (uint32_t)(timer_get_ticks() - start) < 50U) {
                (void)net_poll(16U);
                __asm__ volatile("pause");
            }
        }
    }
    reset_client();
}
