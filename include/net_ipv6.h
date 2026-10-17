#ifndef VANTAOS_NET_IPV6_H
#define VANTAOS_NET_IPV6_H
#include <stdint.h>
#include "net.h"
typedef struct { uint8_t bytes[16]; } net_ipv6_address_t;
int net_ipv6_initialize(void);
void net_ipv6_poll(void);
int net_ipv6_ping(net_device_t*,const net_ipv6_address_t*,uint32_t timeout_ms,uint32_t* elapsed_ms);
int net_ipv6_get_status(net_device_t*,net_ipv6_address_t*,net_ipv6_address_t*,net_ipv6_address_t*);
#endif
