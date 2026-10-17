#ifndef VANTAOS_NET_IPV4_H
#define VANTAOS_NET_IPV4_H
#include <stddef.h>
#include <stdint.h>
#include "net.h"
int net_ipv4_initialize(void);
/* Sends one unfragmented IPv4 packet; resolves the next-hop MAC through ARP. */
int net_ipv4_send(net_device_t* device, uint32_t destination,
                  uint8_t protocol, const void* payload, size_t payload_length);
/* Sends one ICMP echo request and waits for its matching reply. */
int net_ipv4_ping(net_device_t* device, uint32_t destination,
                  uint32_t timeout_ms, uint32_t* elapsed_ms);
#endif
