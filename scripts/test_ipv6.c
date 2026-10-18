#include "net_ipv6.h"
#include "net_ipv4.h"
#include "timer.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static net_device_t device;
static net_ethernet_handler_fn ipv6_handler;
static void *ipv6_context;
static uint32_t ticks = 1000U;
static uint8_t last_tx[1600];
static size_t last_tx_length;
static uint32_t udp6_received;
static uint8_t udp6_payload[32];
static size_t udp6_payload_length;
int net_device_get_ipv4(net_device_t *d, uint32_t *address, uint32_t *mask, uint32_t *gateway) { (void)d; (void)address; (void)mask; (void)gateway; return 0; }
int net_ipv4_send(net_device_t *d, uint32_t destination, uint8_t protocol, const void *payload, size_t length) { (void)d; (void)destination; (void)protocol; (void)payload; (void)length; return 0; }

uint32_t timer_get_ticks(void) { return ticks; }
size_t net_poll(size_t budget) { (void)budget; return 0U; }
uint32_t net_device_count(void) { return 1U; }
net_device_t *net_device_get(uint32_t index) { return index == 0U ? &device : 0; }
int net_ethernet_handler_register(uint16_t type, net_ethernet_handler_fn fn, void *ctx)
{
    if (type != 0x86DDU || !fn || ipv6_handler) return 0;
    ipv6_handler = fn; ipv6_context = ctx; return 1;
}
int net_device_transmit(net_device_t *d, const void *frame, size_t length)
{
    if (d != &device || !frame || length > sizeof(last_tx)) return 0;
    const uint8_t *p = (const uint8_t *)frame;
    for (size_t i = 0; i < length; ++i) last_tx[i] = p[i];
    last_tx_length = length;
    return 1;
}
static uint16_t checksum_bytes(uint32_t sum, const uint8_t *p, size_t n)
{
    while (n >= 2U) { sum += ((uint32_t)p[0] << 8) | p[1]; p += 2; n -= 2U; }
    if (n) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}
static uint16_t icmp6_checksum(const uint8_t src[16], const uint8_t dst[16],
                               const uint8_t *p, size_t n)
{
    uint32_t sum = 0;
    sum = (uint32_t)~checksum_bytes(sum, src, 16U) & 0xFFFFU;
    sum = (uint32_t)~checksum_bytes(sum, dst, 16U) & 0xFFFFU;
    uint8_t pseudo[8] = {(uint8_t)(n >> 24),(uint8_t)(n >> 16),
                         (uint8_t)(n >> 8),(uint8_t)n,0,0,0,58};
    sum = (uint32_t)~checksum_bytes(sum, pseudo, sizeof(pseudo)) & 0xFFFFU;
    sum = (uint32_t)~checksum_bytes(sum, p, n) & 0xFFFFU;
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}
static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v; }
static void make_ip6(uint8_t *ip, const uint8_t src[16], const uint8_t dst[16],
                     uint8_t hop, uint16_t plen)
{
    ip[0]=0x60;ip[1]=ip[2]=ip[3]=0;put16(ip+4,plen);ip[6]=58;ip[7]=hop;
    for(uint32_t i=0;i<16U;i++){ip[8+i]=src[i];ip[24+i]=dst[i];}
}
static void inject_ra(void)
{
    static const uint8_t router_mac[6]={0x02,0xAA,0xBB,0xCC,0xDD,0xEE};
    static const uint8_t src[16]={0xFE,0x80,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    static const uint8_t dst[16]={0xFF,2,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    uint8_t f[14+40+72]={0};f[0]=0x33;f[1]=0x33;f[5]=1;
    for(uint32_t i=0;i<6U;i++){f[6+i]=router_mac[i];}
    put16(f+12,0x86DD);
    uint8_t *ip=f+14,*p=ip+40;make_ip6(ip,src,dst,255,72);
    p[0]=134;p[4]=64;put16(p+6,1800);
    p[16]=3;p[17]=4;p[18]=64;p[19]=0xC0;put32(p+20,3600);put32(p+24,1800);
    p[32]=0x20;p[33]=0x01;p[34]=0x0D;p[35]=0xB8;p[37]=1;
    p[48]=25;p[49]=3;put32(p+52,1800);
    p[56]=0x20;p[57]=0x01;p[58]=0x48;p[59]=0x60;p[60]=0x48;p[61]=0x60;p[62]=0x48;p[63]=0x60;
    p[70]=0x88;p[71]=0x88;
    put16(p+2,icmp6_checksum(src,dst,p,72));
    ipv6_handler(&device,f,sizeof(f),0x86DDU,14U,ipv6_context);
}
static uint16_t udp6_checksum_test(const uint8_t src[16], const uint8_t dst[16], const uint8_t *p, size_t n)
{
    uint32_t sum = 0; const uint8_t *blocks[2] = {src, dst};
    for (uint32_t b = 0; b < 2U; ++b) for (uint32_t i = 0; i < 16U; i += 2U) sum += ((uint32_t)blocks[b][i] << 8) | blocks[b][i + 1U];
    uint8_t pseudo[8] = {(uint8_t)(n >> 24),(uint8_t)(n >> 16),(uint8_t)(n >> 8),(uint8_t)n,0,0,0,17};
    for (uint32_t i = 0; i < 8U; i += 2U) sum += ((uint32_t)pseudo[i] << 8) | pseudo[i + 1U];
    for (size_t i = 0; i + 1U < n; i += 2U) sum += ((uint32_t)p[i] << 8) | p[i + 1U];
    if (n & 1U) sum += (uint32_t)p[n - 1U] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}
static void udp6_handler(net_device_t *d, const uint8_t src[16], const uint8_t dst[16], uint16_t source_port, uint16_t destination_port, const uint8_t *payload, size_t length, void *context)
{
    (void)context; assert(d == &device && src && dst && source_port == 12345U && destination_port == 4321U);
    assert(length <= sizeof(udp6_payload)); for (size_t i = 0; i < length; ++i) udp6_payload[i] = payload[i];
    udp6_payload_length = length; ++udp6_received;
}
static void inject_udp6(const uint8_t global[16], int corrupt_checksum)
{
    static const uint8_t router_mac[6] = {0x02,0xAA,0xBB,0xCC,0xDD,0xEE};
    static const uint8_t source[16] = {0x20,0x01,0x0D,0xB8,0,1,0,0,0,0,0,0,0,0,0,2};
    static const uint8_t message[] = "V6UDP";
    uint8_t f[14U + 40U + 8U + sizeof(message) - 1U] = {0};
    for (uint32_t i = 0; i < 6U; ++i) { f[i] = device.mac[i]; f[6U+i] = router_mac[i]; }
    put16(f + 12U, 0x86DDU); uint8_t *ip = f + 14U, *udp = ip + 40U;
    make_ip6(ip, source, global, 64U, (uint16_t)(8U + sizeof(message) - 1U)); ip[6] = 17U;
    put16(udp, 12345U); put16(udp + 2U, 4321U); put16(udp + 4U, (uint16_t)(8U + sizeof(message) - 1U));
    for (size_t i = 0; i < sizeof(message) - 1U; ++i) udp[8U+i] = message[i];
    put16(udp + 6U, udp6_checksum_test(source, global, udp, 8U + sizeof(message) - 1U));
    if (corrupt_checksum == 1) udp[6] ^= 0x80U;
    else if (corrupt_checksum == 2) udp[6] = udp[7] = 0U;
    else if (corrupt_checksum == 3) {
        put16(udp + 4U, (uint16_t)(9U + sizeof(message) - 1U));
        udp[6] = udp[7] = 0U;
        put16(udp + 6U, udp6_checksum_test(source, global, udp, 8U + sizeof(message) - 1U));
    }
    ipv6_handler(&device, f, sizeof(f), 0x86DDU, 14U, ipv6_context);
}
static void inject_echo(const uint8_t global[16])
{
    static const uint8_t router_mac[6]={0x02,0xAA,0xBB,0xCC,0xDD,0xEE};
    static const uint8_t src[16]={0x20,0x01,0x0D,0xB8,0,1,0,0,0,0,0,0,0,0,0,2};
    uint8_t f[14+40+8]={0};for(uint32_t i=0;i<6U;i++){f[i]=device.mac[i];f[6+i]=router_mac[i];}
    put16(f+12,0x86DD);uint8_t *ip=f+14,*p=ip+40;make_ip6(ip,src,global,64,8);
    p[0]=128;put16(p+4,0x1234);put16(p+6,7);put16(p+2,icmp6_checksum(src,global,p,8));
    ipv6_handler(&device,f,sizeof(f),0x86DDU,14U,ipv6_context);
}
int main(void)
{
    device=(net_device_t){.name="test0",.mac={0x02,0x11,0x22,0x33,0x44,0x55},
                          .mtu=1500,.link_up=1};
    assert(net_ipv6_initialize());assert(ipv6_handler);
    net_ipv6_poll();assert(last_tx_length>=14U+40U+8U);
    inject_ra();
    net_ipv6_address_t ll={{0}},global={{0}},router={{0}};
    assert(net_ipv6_get_status(&device,&ll,&global,&router));
    assert(global.bytes[0]==0U); /* Address stays tentative until DAD completes. */
    ticks+=101U;net_ipv6_poll();
    assert(net_ipv6_get_status(&device,&ll,&global,&router));
    assert(global.bytes[0]==0x20U&&global.bytes[1]==0x01U&&global.bytes[2]==0x0DU&&global.bytes[3]==0xB8U);
    assert(global.bytes[8]==0x00U&&global.bytes[9]==0x11U&&global.bytes[10]==0x22U);
    assert(global.bytes[11]==0xFFU&&global.bytes[12]==0xFEU&&global.bytes[15]==0x55U);
    assert(router.bytes[0]==0xFEU&&router.bytes[1]==0x80U);
    net_ipv6_address_t dns_server={{0}};
    assert(net_ipv6_get_dns_server(&device,&dns_server));
    assert(dns_server.bytes[0]==0x20U&&dns_server.bytes[1]==0x01U&&
           dns_server.bytes[2]==0x48U&&dns_server.bytes[3]==0x60U&&
           dns_server.bytes[14]==0x88U&&dns_server.bytes[15]==0x88U);
    inject_echo(global.bytes);
    assert(last_tx_length==14U+40U+8U);
    assert(last_tx[12]==0x86U&&last_tx[13]==0xDDU&&last_tx[54]==129U);
    assert(icmp6_checksum(last_tx + 22U, last_tx + 38U, last_tx + 54U, 8U) == 0U);
    assert(net_udp6_handler_register(4321U, udp6_handler, 0));
    assert(!net_udp6_handler_register(4321U, udp6_handler, 0));
    inject_udp6(global.bytes, 0);
    assert(udp6_received == 1U && udp6_payload_length == 5U);
    assert(udp6_payload[0] == 'V' && udp6_payload[1] == '6' && udp6_payload[2] == 'U' && udp6_payload[3] == 'D' && udp6_payload[4] == 'P');
    inject_udp6(global.bytes, 1);
    inject_udp6(global.bytes, 2); /* IPv6 UDP checksum may never be zero. */
    inject_udp6(global.bytes, 3); /* UDP length must exactly match IPv6 payload length. */
    assert(udp6_received == 1U);
    static const uint8_t all_nodes[16] = {0xFF,2,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    static const uint8_t out_payload[] = "OUT6";
    assert(net_udp6_send(&device, all_nodes, 4321U, 12345U, out_payload, sizeof(out_payload)-1U));
    assert(last_tx_length == 14U + 40U + 8U + sizeof(out_payload)-1U);
    assert(last_tx[0] == 0x33U && last_tx[5] == 1U && last_tx[20] == 17U);
    assert(last_tx[54] == 0x10U && last_tx[55] == 0xE1U);
    assert(last_tx[56] == 0x30U && last_tx[57] == 0x39U);
    assert(last_tx[60] != 0U || last_tx[61] != 0U);
    assert(udp6_checksum_test(last_tx + 22U, last_tx + 38U, last_tx + 54U,
                               8U + sizeof(out_payload) - 1U) == 0U);
    device.mtu = 1279U;
    assert(!net_udp6_send(&device, all_nodes, 4321U, 12345U, out_payload, sizeof(out_payload)-1U));
    device.mtu = 1280U;
    assert(net_udp6_send(&device, all_nodes, 4321U, 12345U, out_payload, sizeof(out_payload)-1U));
    device.mtu = 1500U;
    assert(net_udp6_handler_unregister(4321U, udp6_handler, 0));
    ticks = 181001U;
    assert(!net_ipv6_get_dns_server(&device, &dns_server)); /* RDNSS lifetime expires. */
    puts("VANTAOS_IPV6_SLAAC_ICMP6_UDP6_TEST_OK");
    return 0;
}
