#ifndef VANTAOS_NET_H
#define VANTAOS_NET_H
#include <stddef.h>
#include <stdint.h>
#include "pci.h"
#define NET_DEVICE_NAME_MAX 16U
#define NET_DEVICE_MAX 8U
#define NET_ETHERNET_HEADER_SIZE 14U
#define NET_ETHERNET_VLAN_HEADER_SIZE 18U
#define NET_MTU_MIN 576U
#define NET_MTU_DEFAULT 1500U
#define NET_FRAME_MAX_SIZE (NET_MTU_DEFAULT + 18U)
#define NET_RX_QUEUE_CAPACITY 16U
#define NET_ETH_HANDLER_MAX 16U
#define NET_UDP_HANDLER_MAX 8U
#define NET_PCI_DRIVER_MAX 16U
#define NET_PCI_BINDING_MAX 16U
typedef struct net_device net_device_t;
typedef struct { int (*transmit)(net_device_t*, const uint8_t*, size_t); } net_device_ops_t;
/* Registered descriptors and ops must outlive registration. transmit is synchronous. */
struct net_device {
    char name[NET_DEVICE_NAME_MAX];
    uint8_t mac[6];
    uint16_t mtu;
    uint8_t link_up;
    const net_device_ops_t* ops;
    void* driver_data;
    /* IPv4 values use host byte order; configuration is explicit, never guessed. */
    uint32_t ipv4_address, ipv4_netmask, ipv4_gateway, ipv4_dns;
    uint8_t ipv4_configured;
};
typedef struct {
    uint64_t rx_frames, rx_bytes, rx_dropped, rx_malformed;
    uint64_t tx_frames, tx_bytes, tx_errors;
} net_device_stats_t;
typedef void (*net_ethernet_handler_fn)(net_device_t*, const uint8_t*, size_t,
                                        uint16_t, size_t, void*);
/* Payload pointers are valid only for the duration of the callback. */
typedef void (*net_udp_handler_fn)(net_device_t*, uint32_t source_address,
                                   uint16_t source_port, uint16_t destination_port,
                                   const uint8_t* payload, size_t payload_length,
                                   void* context);
typedef struct {
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, prog_if;
} net_pci_id_t;
typedef enum {
    NET_PCI_PROBE_NOT_MINE = 0, NET_PCI_PROBE_BOUND = 1, NET_PCI_PROBE_FAILED = 2
} net_pci_probe_result_t;
typedef struct {
    const char* name;
    const net_pci_id_t* ids;
    uint32_t id_count;
    net_pci_probe_result_t (*probe)(const struct pci_device*);
    /* Called by net_poll() in process context; must be bounded and nonblocking. */
    int (*poll)(void);
} net_pci_driver_t;
void net_initialize(void);
int net_device_register(net_device_t*);
int net_device_unregister(net_device_t*);
int net_device_set_link(net_device_t*, int);
int net_device_configure_ipv4(net_device_t*, uint32_t address,
                              uint32_t netmask, uint32_t gateway);
int net_device_get_ipv4(net_device_t*, uint32_t* address,
                        uint32_t* netmask, uint32_t* gateway);
int net_dhcp_configure(net_device_t*, uint32_t timeout_ms,
                       uint32_t* dns_server, uint32_t* lease_seconds);
/* Start DHCP acquisition without blocking the network/USB polling loop. */
int net_dhcp_start(net_device_t*, uint32_t timeout_ms);
void net_dhcp_poll(void);
int net_device_clear_ipv4(net_device_t*);
uint32_t net_device_count(void);
net_device_t* net_device_get(uint32_t);
int net_device_get_stats(net_device_t*, net_device_stats_t*);
int net_device_transmit(net_device_t*, const void*, size_t);
int net_receive_frame(net_device_t*, const void*, size_t);
size_t net_poll(size_t packet_budget);
int net_ethernet_handler_register(uint16_t, net_ethernet_handler_fn, void*);
int net_ethernet_handler_unregister(uint16_t, net_ethernet_handler_fn, void*);
int net_udp_initialize(void);
/* Sends a UDP datagram through IPv4; may block briefly for ARP resolution. */
int net_udp_send(net_device_t*, uint32_t destination_address,
                 uint16_t source_port, uint16_t destination_port,
                 const void* payload, size_t payload_length);
int net_udp_handler_register(uint16_t, net_udp_handler_fn, void*);
int net_udp_handler_unregister(uint16_t, net_udp_handler_fn, void*);
/* Internal IPv4-to-UDP dispatch; the IP header has already been validated. */
void net_udp_receive_ipv4(net_device_t*, const uint8_t*, size_t, size_t);
int net_pci_driver_register(const net_pci_driver_t*);
uint32_t net_probe_pci_devices(void);
size_t net_pci_poll_devices(void);
const char* net_pci_bound_driver(const struct pci_device*);
#endif
