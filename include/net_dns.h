#ifndef VANTAOS_NET_DNS_H
#define VANTAOS_NET_DNS_H
#include <stdint.h>
#include "net.h"
#include "net_ipv6.h"
/* Resolves an A record using the DNS server learned through DHCP. */
int net_dns_resolve(net_device_t* device, const char* hostname,
                    uint32_t* ipv4_out, uint32_t timeout_ms);
/* Resolves an AAAA record using the first unexpired RA RDNSS server. */
int net_dns_resolve_aaaa(net_device_t* device, const char* hostname,
                         net_ipv6_address_t* ipv6_out, uint32_t timeout_ms);
#endif
