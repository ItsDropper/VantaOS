/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "net_dns.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DNS_PORT 53U
#define CLIENT_PORT 53000U

static net_udp_handler_fn registered_handler;
static void *registered_context;
static uint16_t registered_port;
static uint32_t fake_ticks;
static int response_kind;
static uint32_t send_count;

uint32_t timer_get_ticks(void) { return fake_ticks++; }

int net_udp_handler_register(uint16_t port, net_udp_handler_fn fn, void *context)
{
    if (registered_handler || !fn || port != CLIENT_PORT) return 0;
    registered_port = port;
    registered_handler = fn;
    registered_context = context;
    return 1;
}

int net_udp_handler_unregister(uint16_t port, net_udp_handler_fn fn, void *context)
{
    if (port != registered_port || fn != registered_handler ||
        context != registered_context) return 0;
    registered_handler = 0;
    registered_context = 0;
    registered_port = 0;
    return 1;
}

int net_udp_send(net_device_t *device, uint32_t destination, uint16_t source_port,
                 uint16_t destination_port, const void *payload, size_t length)
{
    ++send_count;
    const uint8_t *query = (const uint8_t *)payload;
    uint8_t reply[64] = {0};
    size_t n = 0;
    if (!device || destination != device->ipv4_dns || source_port != CLIENT_PORT ||
        destination_port != DNS_PORT || !query || length < 12U || !registered_handler)
        return 0;

    /* DNS response header followed by the echoed question for example.com. */
    reply[0] = query[0]; reply[1] = query[1];
    reply[2] = 0x81; reply[3] = 0x80;
    reply[5] = 1; reply[7] = 1;
    n = 12;
    static const uint8_t question[] = {
        7,'e','x','a','m','p','l','e',3,'c','o','m',0,0,1,0,1
    };
    memcpy(reply + n, question, sizeof(question));
    n += sizeof(question);

    if (response_kind == 1) {
        /* Answer owner points to itself: an invalid compression-pointer loop. */
        size_t owner_offset = n;
        reply[n++] = 0xC0; reply[n++] = (uint8_t)owner_offset;
        reply[n++] = 0; reply[n++] = 1;
        reply[n++] = 0; reply[n++] = 1;
        n += 4; /* TTL */
        reply[n++] = 0; reply[n++] = 4;
        reply[n++] = 192; reply[n++] = 0; reply[n++] = 2; reply[n++] = 1;
    } else if (response_kind == 2) {
        reply[n++] = 0xC0; reply[n++] = 0xFF; /* Out-of-message pointer. */
        n += 8;
        reply[n++] = 0; reply[n++] = 4;
        reply[n++] = 192; reply[n++] = 0; reply[n++] = 2; reply[n++] = 1;
    } else if (response_kind == 3) {
        reply[n++] = 0xC0; reply[n++] = 0x0C;
        reply[n++] = 0; reply[n++] = 1;
        reply[n++] = 0; reply[n++] = 1;
        n += 4;
        reply[n++] = 0; reply[n++] = 4;
        reply[n++] = 192; reply[n++] = 0; reply[n++] = 2; /* Truncated RDATA. */
    } else if (response_kind == 4) {
        /* A forward pointer to a valid root label appended after this RR. */
        reply[n++] = 0xC0; reply[n++] = 45;
        reply[n++] = 0; reply[n++] = 1;
        reply[n++] = 0; reply[n++] = 1;
        n += 4;
        reply[n++] = 0; reply[n++] = 4;
        reply[n++] = 192; reply[n++] = 0; reply[n++] = 2; reply[n++] = 1;
        reply[n++] = 0; /* Valid root label at offset 45. */
    } else if (response_kind == 5) {
        /* Valid A record for an unrelated owner must not answer example.com. */
        reply[n++] = 7; memcpy(reply + n, "attacker", 8); n += 8;
        reply[n++] = 3; memcpy(reply + n, "net", 3); n += 3; reply[n++] = 0;
        reply[n++] = 0; reply[n++] = 1;
        reply[n++] = 0; reply[n++] = 1;
        reply[n++] = 0; reply[n++] = 0; reply[n++] = 0; reply[n++] = 60;
        reply[n++] = 0; reply[n++] = 4;
        reply[n++] = 203; reply[n++] = 0; reply[n++] = 113; reply[n++] = 77;
    } else {
        reply[n++] = 0xC0; reply[n++] = 0x0C;
        reply[n++] = 0; reply[n++] = 1;
        reply[n++] = 0; reply[n++] = 1;
        reply[n++] = 0; reply[n++] = 0; reply[n++] = 0; reply[n++] = 60; /* TTL 60 seconds. */
        reply[n++] = 0; reply[n++] = 4;
        reply[n++] = 192; reply[n++] = 0; reply[n++] = 2; reply[n++] = 1;
    }

    registered_handler(device, device->ipv4_dns, DNS_PORT, registered_port,
                       reply, n, registered_context);
    return 1;
}

size_t net_poll(size_t packet_budget) { (void)packet_budget; return 0; }

static int run_case(int kind, int expected_success)
{
    static net_device_t device;
    device = (net_device_t){0};
    uint32_t result = 0xDEADBEEFU;
    device.link_up = 1;
    device.ipv4_configured = 1;
    device.ipv4_dns = 0xC0000201U + (uint32_t)kind;
    response_kind = kind;
    fake_ticks = 0;
    int success = net_dns_resolve(&device, "example.com", &result, 1000U);
    if (success != expected_success) return 0;
    if (expected_success && result != 0xC0000201U) return 0;
    if (!expected_success && result != 0xDEADBEEFU) return 0;
    if (registered_handler || registered_context || registered_port) return 0;
    return 1;
}

int main(void)
{
    if (!run_case(0, 1)) {
        fputs("valid compressed DNS A response was rejected\n", stderr);
        return 1;
    }
    uint32_t sends_after_first_lookup = send_count;
    if (!run_case(0, 1) || send_count != sends_after_first_lookup) {
        fputs("DNS positive TTL cache did not suppress the repeated query\n", stderr);
        return 6;
    }
    if (!run_case(1, 0)) {
        fputs("DNS compression pointer loop was accepted\n", stderr);
        return 2;
    }
    if (!run_case(2, 0)) {
        fputs("out-of-range DNS compression pointer was accepted\n", stderr);
        return 3;
    }
    if (!run_case(3, 0)) {
        fputs("truncated DNS A record was accepted\n", stderr);
        return 4;
    }
    if (!run_case(4, 0)) {
        fputs("forward DNS compression pointer was accepted\\n", stderr);
        return 5;
    }
    if (!run_case(5, 0)) {
        fputs("DNS A record for an unrelated owner was accepted (cache-poisoning regression)\\n", stderr);
        return 7;
    }
    puts("DNS question-owner binding, compressed A response, and malformed packet tests passed");
    return 0;
}
