#ifndef VANTAOS_NET_TCP_H
#define VANTAOS_NET_TCP_H

#include <stddef.h>
#include <stdint.h>
#include "net.h"

#define NET_TCP_RX_CAPACITY 8192U

/* One bounded, synchronous TCP client connection is supported at a time. */
int net_tcp_initialize(void);
int net_tcp_connect(net_device_t *device, uint32_t destination,
                    uint16_t destination_port, uint32_t timeout_ms);
int net_tcp_send(net_device_t *device, const void *data, size_t length,
                 uint32_t timeout_ms);
/* Returns bytes read, 0 for orderly peer EOF, or -1 on error/timeout. */
int net_tcp_receive(net_device_t *device, void *buffer, size_t capacity,
                    uint32_t timeout_ms);
void net_tcp_close(net_device_t *device);
/* Called only by the validated IPv4 receive path. */
void net_tcp_receive_ipv4(net_device_t *device, const uint8_t *ip,
                          size_t ip_header_length, size_t ip_total_length);

#endif
