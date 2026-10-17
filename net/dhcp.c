#include "net.h"
#include "net_arp.h"
#include "timer.h"
#include <stddef.h>
#include <stdint.h>

#define DHCP_CLIENT_PORT 68U
#define DHCP_SERVER_PORT 67U
#define DHCP_FIXED_SIZE 236U
#define DHCP_COOKIE_SIZE 4U
#define DHCP_MAX_OPTIONS 312U
#define DHCP_ETH_HEADER 14U
#define DHCP_IPV4_HEADER 20U
#define DHCP_UDP_HEADER 8U
#define DHCP_XID_OFFSET 4U
#define DHCP_YIADDR_OFFSET 16U
#define DHCP_CHADDR_OFFSET 28U
#define DHCP_COOKIE_OFFSET 236U
#define DHCP_OPT_PAD 0U
#define DHCP_OPT_END 255U
#define DHCP_OPT_MASK 1U
#define DHCP_OPT_ROUTER 3U
#define DHCP_OPT_DNS 6U
#define DHCP_OPT_REQUESTED_IP 50U
#define DHCP_OPT_LEASE 51U
#define DHCP_OPT_MESSAGE_TYPE 53U
#define DHCP_OPT_SERVER_ID 54U
#define DHCP_OPT_PARAM_LIST 55U
#define DHCP_OPT_CLIENT_ID 61U
#define DHCP_DISCOVER 1U
#define DHCP_OFFER 2U
#define DHCP_REQUEST 3U
#define DHCP_ACK 5U
#define DHCP_NAK 6U
#define DHCP_MAGIC 0x63825363U
#define DHCP_RETRY_TICKS 300U

typedef struct {
    net_device_t* device;
    uint32_t xid;
    uint32_t offered_ip;
    uint32_t server_ip;
    uint32_t mask;
    uint32_t gateway;
    uint32_t dns;
    uint32_t lease_seconds;
    uint8_t message_type;
    uint8_t active;
    uint8_t offered;
    uint8_t acknowledged;
    uint8_t rejected;
    uint8_t renewing;
    uint8_t lease_valid;
    uint8_t retry_count;
    uint32_t lease_address, lease_started, renew_at, rebind_at, expires_at, next_retry;
} dhcp_state_t;

static dhcp_state_t state;
static int handler_registered;

static uint16_t be16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void put16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}

static void put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static uint16_t checksum(const uint8_t* p, size_t length)
{
    uint32_t sum = 0;
    while (length >= 2U) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2; length -= 2U;
    }
    if (length) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}

static void option(uint8_t* packet, size_t* length, uint8_t code,
                   const uint8_t* value, uint8_t value_length)
{
    packet[(*length)++] = code;
    packet[(*length)++] = value_length;
    for (uint32_t i = 0; i < value_length; ++i)
        packet[(*length)++] = value[i];
}

static int send_message(uint8_t message_type)
{
    if (!state.device || !state.device->link_up) return 0;

    uint8_t frame[DHCP_ETH_HEADER + DHCP_IPV4_HEADER + DHCP_UDP_HEADER +
                  DHCP_FIXED_SIZE + DHCP_COOKIE_SIZE + DHCP_MAX_OPTIONS];
    size_t frame_length = DHCP_ETH_HEADER + DHCP_IPV4_HEADER + DHCP_UDP_HEADER;
    uint8_t* eth = frame;
    uint8_t* ip = eth + DHCP_ETH_HEADER;
    uint8_t* udp = ip + DHCP_IPV4_HEADER;
    uint8_t* bootp = udp + DHCP_UDP_HEADER;
    for (uint32_t i = 0; i < 6U; ++i) eth[i] = 0xFFU;
    for (uint32_t i = 0; i < 6U; ++i) eth[6U + i] = state.device->mac[i];
    uint32_t source_ip = state.renewing ? state.lease_address : 0U;
    uint32_t destination_ip = 0xFFFFFFFFU;
    if (state.renewing == 1U && state.server_ip) {
        uint32_t mask = state.device->ipv4_netmask;
        uint32_t next_hop = ((state.server_ip & mask) == (state.lease_address & mask))
            ? state.server_ip : state.device->ipv4_gateway;
        uint8_t destination_mac[6];
        if (next_hop && net_arp_resolve(state.device, next_hop, destination_mac, 1000U)) {
            for (uint32_t i = 0; i < 6U; ++i) eth[i] = destination_mac[i];
            destination_ip = state.server_ip;
        } else {
            state.renewing = 2U;
        }
    }
    put16(eth + 12U, 0x0800U);

    for (size_t i = 0; i < DHCP_IPV4_HEADER; ++i) ip[i] = 0;
    ip[0] = 0x45U; ip[1] = 0;
    put16(ip + 2U, 0);
    put16(ip + 4U, (uint16_t)state.xid);
    put16(ip + 6U, 0x4000U);
    ip[8] = 64U; ip[9] = 17U;
    put32(ip + 12U, source_ip); put32(ip + 16U, destination_ip);

    put16(udp, DHCP_CLIENT_PORT); put16(udp + 2U, DHCP_SERVER_PORT);
    put16(udp + 6U, 0U); /* IPv4 UDP checksum may legally be disabled. */

    for (size_t i = 0; i < DHCP_FIXED_SIZE; ++i) bootp[i] = 0;
    bootp[0] = 1U; bootp[1] = 1U; bootp[2] = 6U;
    put32(bootp + DHCP_XID_OFFSET, state.xid);
    put16(bootp + 10U, state.renewing == 1U ? 0U : 0x8000U); /* Unicast at T1; broadcast for discovery/rebinding. */
    if (state.renewing) put32(bootp + 12U, state.lease_address);
    for (uint32_t i = 0; i < 6U; ++i)
        bootp[DHCP_CHADDR_OFFSET + i] = state.device->mac[i];
    put32(bootp + DHCP_COOKIE_OFFSET, DHCP_MAGIC);

    size_t option_length = DHCP_COOKIE_OFFSET + DHCP_COOKIE_SIZE;
    uint8_t type = message_type;
    option(bootp, &option_length, DHCP_OPT_MESSAGE_TYPE, &type, 1U);
    uint8_t client_id[7] = {1U, 0, 0, 0, 0, 0, 0};
    for (uint32_t i = 0; i < 6U; ++i) client_id[i + 1U] = state.device->mac[i];
    option(bootp, &option_length, DHCP_OPT_CLIENT_ID, client_id, sizeof(client_id));
    if (message_type == DHCP_REQUEST && !state.renewing) {
        uint8_t requested[4], server[4];
        put32(requested, state.offered_ip);
        put32(server, state.server_ip);
        option(bootp, &option_length, DHCP_OPT_REQUESTED_IP, requested, 4U);
        option(bootp, &option_length, DHCP_OPT_SERVER_ID, server, 4U);
    }
    uint8_t requested_options[] = {DHCP_OPT_MASK, DHCP_OPT_ROUTER,
                                   DHCP_OPT_DNS, DHCP_OPT_LEASE,
                                   DHCP_OPT_SERVER_ID};
    option(bootp, &option_length, DHCP_OPT_PARAM_LIST, requested_options,
           sizeof(requested_options));
    bootp[option_length++] = DHCP_OPT_END;

    size_t bootp_length = option_length;
    size_t udp_length = DHCP_UDP_HEADER + bootp_length;
    size_t ip_length = DHCP_IPV4_HEADER + udp_length;
    if (ip_length > 1500U) return 0;
    put16(udp + 4U, (uint16_t)udp_length);
    put16(ip + 2U, (uint16_t)ip_length);
    put16(ip + 10U, checksum(ip, DHCP_IPV4_HEADER));
    frame_length += udp_length;
    return net_device_transmit(state.device, frame, frame_length);
}

static int parse_options(const uint8_t* packet, size_t length)
{
    size_t at = DHCP_COOKIE_OFFSET + DHCP_COOKIE_SIZE;
    int message_seen = 0;
    while (at < length) {
        uint8_t code = packet[at++];
        if (code == DHCP_OPT_PAD) continue;
        if (code == DHCP_OPT_END) break;
        if (at >= length) return 0;
        uint8_t n = packet[at++];
        if ((size_t)n > length - at) return 0;
        const uint8_t* v = packet + at;
        switch (code) {
        case DHCP_OPT_MESSAGE_TYPE:
            if (n != 1U) return 0;
            state.message_type = v[0]; message_seen = 1; break;
        case DHCP_OPT_MASK:
            if (n != 4U) return 0;
            state.mask = be32(v); break;
        case DHCP_OPT_ROUTER:
            if (n < 4U || (n % 4U) != 0U) return 0;
            state.gateway = be32(v); break;
        case DHCP_OPT_DNS:
            if (n < 4U || (n % 4U) != 0U) return 0;
            state.dns = be32(v); break;
        case DHCP_OPT_SERVER_ID:
            if (n != 4U) return 0;
            state.server_ip = be32(v); break;
        case DHCP_OPT_LEASE:
            if (n != 4U) return 0;
            state.lease_seconds = be32(v); break;
        default: break;
        }
        at += n;
    }
    return message_seen;
}

static void receive_dhcp(net_device_t* device, uint32_t source_address,
                         uint16_t source_port, uint16_t destination_port,
                         const uint8_t* payload, size_t length, void* context)
{
    (void)context;
    if (!state.active || device != state.device || source_port != DHCP_SERVER_PORT ||
        destination_port != DHCP_CLIENT_PORT || !payload ||
        length < DHCP_COOKIE_OFFSET + DHCP_COOKIE_SIZE + 3U)
        return;
    if (payload[0] != 2U || payload[1] != 1U || payload[2] != 6U ||
        be32(payload + DHCP_XID_OFFSET) != state.xid ||
        be32(payload + DHCP_COOKIE_OFFSET) != DHCP_MAGIC)
        return;
    for (uint32_t i = 0; i < 6U; ++i)
        if (payload[DHCP_CHADDR_OFFSET + i] != device->mac[i]) return;

    uint32_t yiaddr = be32(payload + DHCP_YIADDR_OFFSET);
    uint32_t expected_server = state.server_ip;
    state.message_type = 0;
    if (!parse_options(payload, length)) return;
    if (state.message_type == DHCP_ACK && expected_server &&
        state.server_ip != expected_server)
        return;
    if (!state.server_ip) state.server_ip = source_address;
    if (state.message_type == DHCP_NAK) {
        state.rejected = 1U;
        return;
    }
    if (state.message_type == DHCP_OFFER && yiaddr) {
        state.offered_ip = yiaddr;
        state.offered = 1U;
        return;
    }
    if (state.message_type == DHCP_ACK && yiaddr == state.offered_ip &&
        state.offered && state.mask) {
        state.offered_ip = yiaddr;
        state.acknowledged = 1U;
    }
}

int net_dhcp_configure(net_device_t* device, uint32_t timeout_ms,
                       uint32_t* dns_server, uint32_t* lease_seconds)
{
    if (!device || !device->link_up || timeout_ms < 1000U ||
        timeout_ms > 120000U)
        return 0;
    if (!handler_registered) {
        if (!net_udp_handler_register(DHCP_CLIENT_PORT, receive_dhcp, 0))
            return 0;
        handler_registered = 1;
    }

    state = (dhcp_state_t){0};
    state.device = device;
    state.active = 1U;
    state.xid = 0x564E5400U;
    for (uint32_t i = 0; i < 6U; ++i)
        state.xid = (state.xid * 33U) ^ device->mac[i];
    if (!state.xid) state.xid = 1U;

    uint32_t start = timer_get_ticks();
    uint32_t timeout_ticks = timeout_ms / 10U; /* PIT is initialized at 100 Hz. */
    uint32_t next_retry = start;
    uint8_t requested = 0;
    int attempted = 0;
    while ((uint32_t)(timer_get_ticks() - start) < timeout_ticks) {
        uint32_t now = timer_get_ticks();
        if (!attempted || (state.offered && !requested) ||
            (uint32_t)(now - next_retry) >= DHCP_RETRY_TICKS) {
            uint8_t kind = state.offered ? DHCP_REQUEST : DHCP_DISCOVER;
            if (kind == DHCP_REQUEST) requested = 1U;
            (void)send_message(kind);
            attempted = 1;
            next_retry = now;
        }
        (void)net_poll(16U);
        if (state.rejected) break;
        if (state.acknowledged) break;
        __asm__ volatile ("pause");
    }

    state.active = 0U;
    int configured = 0;
    if (state.acknowledged && state.offered && requested && state.mask &&
        net_device_configure_ipv4(device, state.offered_ip, state.mask,
                                  state.gateway)) {
        if (dns_server) *dns_server = state.dns;
        if (!state.lease_seconds) state.lease_seconds = 3600U;
        if (lease_seconds) *lease_seconds = state.lease_seconds;
        state.lease_address = state.offered_ip;
        state.lease_valid = 1U;
        state.renewing = 0U;
        state.lease_started = timer_get_ticks();
        uint32_t ticks = state.lease_seconds > 21474836U ? 2147483600U : state.lease_seconds * 100U;
        if (ticks < 300U) ticks = 300U;
        state.renew_at = state.lease_started + ticks / 2U;
        state.rebind_at = state.lease_started + (ticks * 7U) / 8U;
        state.expires_at = state.lease_started + ticks;
        configured = 1;
    } else {
        state.lease_valid = 0U;
        state.device = 0;
    }
    return configured;
}

void net_dhcp_poll(void)
{
    if (!state.device || !state.lease_valid || !state.device->link_up) return;
    uint32_t now = timer_get_ticks();
    if (state.active && state.renewing) {
        if (state.acknowledged) {
            if (state.mask && net_device_configure_ipv4(state.device, state.offered_ip,
                    state.mask, state.gateway)) {
                state.device->ipv4_dns = state.dns;
                state.lease_address = state.offered_ip;
                if (!state.lease_seconds) state.lease_seconds = 3600U;
                state.lease_started = now;
                uint32_t ticks = state.lease_seconds > 21474836U ? 2147483600U : state.lease_seconds * 100U;
                if (ticks < 300U) ticks = 300U;
                state.renew_at = now + ticks / 2U;
                state.rebind_at = now + (ticks * 7U) / 8U;
                state.expires_at = now + ticks;
            }
            state.active = 0U; state.renewing = 0U;
            return;
        }
        if (state.rejected || (int32_t)(now - state.expires_at) >= 0) {
            (void)net_device_clear_ipv4(state.device);
            state.lease_valid = 0U; state.active = 0U; state.renewing = 0U;
            return;
        }
        if (state.renewing == 1U && (int32_t)(now - state.rebind_at) >= 0) {
            state.renewing = 2U; state.next_retry = now;
        }
        if ((int32_t)(now - state.next_retry) >= 0) {
            uint32_t shift = state.retry_count > 3U ? 3U : state.retry_count;
            uint32_t delay = DHCP_RETRY_TICKS << shift;
            if (delay > 2400U) delay = 2400U;
            ++state.retry_count; state.next_retry = now + delay;
            (void)send_message(DHCP_REQUEST);
        }
        return;
    }
    if ((int32_t)(now - state.expires_at) >= 0) {
        (void)net_device_clear_ipv4(state.device); state.lease_valid = 0U; return;
    }
    if ((int32_t)(now - state.renew_at) < 0) return;
    state.active = 1U; state.renewing = 1U; state.acknowledged = 0U; state.rejected = 0U;
    state.offered = 1U; state.offered_ip = state.lease_address;
    state.mask = state.device->ipv4_netmask; state.gateway = state.device->ipv4_gateway;
    state.dns = state.device->ipv4_dns; state.message_type = 0U;
    state.retry_count = 1U; state.next_retry = now + DHCP_RETRY_TICKS;
    state.xid = now ^ 0x564E5400U;
    for (uint32_t i = 0; i < 6U; ++i) state.xid = (state.xid * 33U) ^ state.device->mac[i];
    if (!state.xid) state.xid = 1U;
    (void)send_message(DHCP_REQUEST);
}
