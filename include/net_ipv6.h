#ifndef VANTAOS_NET_IPV6_H
#define VANTAOS_NET_IPV6_H
#include <stddef.h>
#include <stdint.h>
#include "net.h"
typedef struct { uint8_t bytes[16]; } net_ipv6_address_t;

/* IPv6 requires a 1280-byte link MTU; the current packet buffer is 1500 bytes. */
static inline int net_ipv6_payload_fits_mtu(uint16_t mtu, size_t payload_length)
{
    return mtu >= 1280U && mtu <= NET_MTU_DEFAULT &&
           payload_length != 0U && payload_length <= (size_t)mtu - 40U;
}
int net_ipv6_initialize(void);
void net_ipv6_poll(void);
int net_ipv6_ping(net_device_t*,const net_ipv6_address_t*,uint32_t timeout_ms,uint32_t* elapsed_ms);
int net_ipv6_get_status(net_device_t*,net_ipv6_address_t*,net_ipv6_address_t*,net_ipv6_address_t*);
/* Internal outbound IPv6 payload path; currently supports UDP (next header 17). */
int net_ipv6_send_payload(net_device_t*, const uint8_t destination[16], const uint8_t source[16], uint8_t next_header, const void* payload, size_t payload_length);
#endif
