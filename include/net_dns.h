#ifndef VANTAOS_NET_DNS_H
#define VANTAOS_NET_DNS_H
#include <stdint.h>
#include "net.h"
/* Resolves an A record using the DNS server learned through DHCP. */
int net_dns_resolve(net_device_t* device, const char* hostname,
                    uint32_t* ipv4_out, uint32_t timeout_ms);
#endif
