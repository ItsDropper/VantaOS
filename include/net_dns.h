/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTAOS_NET_DNS_H
#define VANTAOS_NET_DNS_H
#include <stdint.h>
#include "net.h"
/* Resolves an A record using the DNS server learned through DHCP. */
int net_dns_resolve(net_device_t* device, const char* hostname,
                    uint32_t* ipv4_out, uint32_t timeout_ms);
#endif
