#ifndef VANTAOS_NET_ARP_H
#define VANTAOS_NET_ARP_H
#include <stdint.h>
#include "net.h"
int net_arp_initialize(void);
/* Resolves an on-link next hop using the bounded ARP cache and request retry. */
int net_arp_resolve(net_device_t* device, uint32_t ipv4_address,
                    uint8_t mac_out[6], uint32_t timeout_ms);
#endif
