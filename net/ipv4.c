#include "net.h"
#include "net_arp.h"
#include "net_ipv4.h"
#include "net_tcp.h"
#include "spinlock.h"
#include "timer.h"
#include "serial.h"
#include <stddef.h>
#include <stdint.h>
#define IPV4_ETHERTYPE 0x0800U
#define IPV4_PROTOCOL_ICMP 1U
#define IPV4_PROTOCOL_UDP 17U
#define IPV4_PROTOCOL_TCP 6U

static spinlock_t ping_lock = SPINLOCK_INITIALIZER;
static uint32_t ping_target;
static uint16_t ping_identifier, ping_sequence;
static uint8_t ping_active, ping_received;
static uint16_t read_be16(const uint8_t* p){return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static uint32_t read_be32(const uint8_t* p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void write_be16(uint8_t* p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void write_be32(uint8_t* p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static uint16_t checksum(const uint8_t* p,size_t n)
{
    uint32_t sum=0;
    while(n>=2U){sum+=((uint32_t)p[0]<<8)|p[1];p+=2;n-=2U;}
    if(n)sum+=(uint32_t)p[0]<<8;
    while(sum>>16)sum=(sum&0xFFFFU)+(sum>>16);
    return (uint16_t)~sum;
}
static int valid_source_mac(const uint8_t* m)
{
    uint8_t any=0;for(uint32_t i=0;i<6;i++)any|=m[i];
    return any!=0U&&!(m[0]&1U);
}
static int unicast_ipv4(uint32_t ip)
{
    uint8_t first=(uint8_t)(ip>>24);
    return ip!=0U&&ip!=0xFFFFFFFFU&&first!=0U&&first!=127U&&first<224U;
}
static void ipv4_receive(net_device_t* d,const uint8_t* frame,size_t length,
                         uint16_t type,size_t ethernet_header,void* context)
{
    (void)type;(void)context;
    if(!d||!frame||(ethernet_header!=NET_ETHERNET_HEADER_SIZE&&ethernet_header!=NET_ETHERNET_VLAN_HEADER_SIZE)||
       length<ethernet_header+20U||
       read_be16(frame+(ethernet_header==NET_ETHERNET_VLAN_HEADER_SIZE?16U:12U))!=IPV4_ETHERTYPE)return;
    const uint8_t* ip=frame+ethernet_header;
    uint8_t version=ip[0]>>4;size_t ihl=(size_t)(ip[0]&0x0FU)*4U;
    if(version!=4U||ihl<20U||ihl>60U||length<ethernet_header+ihl)return;
    uint16_t total=read_be16(ip+2);
    if(total<ihl+8U||total>length-ethernet_header||total>NET_MTU_DEFAULT||
       checksum(ip,ihl)!=0U)return;
    uint16_t fragments=read_be16(ip+6);
    if(fragments&0xBFFFU)return;
    if(ip[9]!=IPV4_PROTOCOL_ICMP&&ip[9]!=IPV4_PROTOCOL_UDP&&ip[9]!=IPV4_PROTOCOL_TCP)return;
    uint32_t local_ip=0,local_mask=0,source_ip=read_be32(ip+12),dest_ip=read_be32(ip+16);
    int configured=net_device_get_ipv4(d,&local_ip,&local_mask,0);
    int dhcp_broadcast=ip[9]==IPV4_PROTOCOL_UDP&&
        dest_ip==0xFFFFFFFFU&&total>=ihl+8U&&
        read_be16(ip+ihl+2U)==68U;
    if((!configured&&!dhcp_broadcast)||(configured&&dest_ip!=local_ip&&!dhcp_broadcast)||
       !unicast_ipv4(source_ip)||(configured&&source_ip==local_ip)||ip[8]<=1U||
       !valid_source_mac(frame+6)||
       (configured&&((source_ip&local_mask)==(local_ip&local_mask)&&
        (((source_ip&~local_mask)==0U)||((source_ip&~local_mask)==~local_mask)))))return;
    size_t icmp_length=(size_t)total-ihl;
    const uint8_t* icmp=ip+ihl;
    if(ip[9]==IPV4_PROTOCOL_UDP){
        net_udp_receive_ipv4(d,ip,ihl,total);
        return;
    }
    if(ip[9]==IPV4_PROTOCOL_TCP){
        net_tcp_receive_ipv4(d,ip,ihl,total);
        return;
    }
    if(icmp_length<8U||checksum(icmp,icmp_length)!=0U)return;
    if(icmp[0]==0U&&icmp[1]==0U){
        uint64_t flags=spinlock_lock_irqsave(&ping_lock);
        if(ping_active==1U&&source_ip==ping_target&&
           read_be16(icmp+4U)==ping_identifier&&
           read_be16(icmp+6U)==ping_sequence)
            ping_received=1U;
        spinlock_unlock_irqrestore(&ping_lock,flags);
        return;
    }
    if(icmp[0]!=8U||icmp[1]!=0U)return;
    uint8_t out[NET_ETHERNET_VLAN_HEADER_SIZE+NET_MTU_DEFAULT];
    for(uint32_t i=0;i<12U;i++)out[i]=frame[i];
    if(ethernet_header==NET_ETHERNET_VLAN_HEADER_SIZE){
        for(uint32_t i=12U;i<16U;i++)out[i]=frame[i];
        write_be16(out+16U,IPV4_ETHERTYPE);
    }else write_be16(out+12U,IPV4_ETHERTYPE);
    for(uint32_t i=0;i<6U;i++){out[i]=frame[6U+i];out[6U+i]=d->mac[i];}
    uint8_t* reply=out+ethernet_header;
    for(size_t i=0;i<total;i++)reply[i]=ip[i];
    write_be32(reply+12,local_ip);write_be32(reply+16,source_ip);
    reply[8]=64U;reply[10]=0;reply[11]=0;
    write_be16(reply+10,checksum(reply,ihl));
    uint8_t* reply_icmp=reply+ihl;
    reply_icmp[0]=0;reply_icmp[2]=0;reply_icmp[3]=0;
    write_be16(reply_icmp+2,checksum(reply_icmp,icmp_length));
    int sent=net_device_transmit(d,out,ethernet_header+(size_t)total);
#ifdef VANTAOS_SELF_TEST
    if(sent)serial_write("VANTAOS_ICMP_ECHO_REPLY_OK\n");
#else
    (void)sent;
#endif
}
int net_ipv4_initialize(void)
{
    return net_ethernet_handler_register(IPV4_ETHERTYPE,ipv4_receive,0);
}


int net_ipv4_send(net_device_t* device, uint32_t destination,
                  uint8_t protocol, const void* payload, size_t payload_length)
{
    if (!device || !payload || !device->link_up ||
        (protocol != IPV4_PROTOCOL_ICMP && protocol != IPV4_PROTOCOL_UDP &&
         protocol != IPV4_PROTOCOL_TCP) ||
        payload_length == 0U || payload_length > (size_t)device->mtu - 20U)
        return 0;

    uint32_t source = 0, mask = 0, gateway = 0;
    if (!net_device_get_ipv4(device, &source, &mask, &gateway) ||
        !unicast_ipv4(source) || !unicast_ipv4(destination) ||
        source == destination)
        return 0;

    uint32_t next_hop = ((source & mask) == (destination & mask))
                      ? destination : gateway;
    if (!next_hop || !unicast_ipv4(next_hop))
        return 0;

    uint8_t destination_mac[6];
    if (!net_arp_resolve(device, next_hop, destination_mac, 3000U))
        return 0;

    uint8_t frame[NET_ETHERNET_HEADER_SIZE + NET_MTU_DEFAULT] = {0};
    for (uint32_t i = 0; i < 6U; ++i) {
        frame[i] = destination_mac[i];
        frame[6U + i] = device->mac[i];
    }
    write_be16(frame + 12U, IPV4_ETHERTYPE);
    uint8_t* ip = frame + NET_ETHERNET_HEADER_SIZE;
    ip[0] = 0x45U;
    ip[1] = 0U;
    write_be16(ip + 2U, (uint16_t)(20U + payload_length));
    static uint16_t packet_id = 1U;
    uint16_t id = __atomic_fetch_add(&packet_id, 1U, __ATOMIC_RELAXED);
    write_be16(ip + 4U, id);
    write_be16(ip + 6U, 0x4000U); /* Don't fragment; fragmentation isn't implemented. */
    ip[8] = 64U;
    ip[9] = protocol;
    write_be32(ip + 12U, source);
    write_be32(ip + 16U, destination);
    write_be16(ip + 10U, checksum(ip, 20U));
    const uint8_t* input = (const uint8_t*)payload;
    for (size_t i = 0; i < payload_length; ++i) ip[20U + i] = input[i];
    return net_device_transmit(device, frame,
                               NET_ETHERNET_HEADER_SIZE + 20U + payload_length);
}


int net_ipv4_ping(net_device_t* device, uint32_t destination,
                  uint32_t timeout_ms, uint32_t* elapsed_ms)
{
    if (!device || !device->link_up || timeout_ms < 100U ||
        timeout_ms > 30000U || !device->ipv4_configured ||
        !unicast_ipv4(destination))
        return 0;

    uint64_t flags = spinlock_lock_irqsave(&ping_lock);
    if (ping_active) {
        spinlock_unlock_irqrestore(&ping_lock, flags);
        return 0;
    }
    ping_target = destination;
    ping_identifier = (uint16_t)(timer_get_ticks() ^ device->mac[1] ^
                                 ((uint16_t)device->mac[3] << 8));
    if (!ping_identifier) ping_identifier = 1U;
    ping_sequence = (uint16_t)(timer_get_ticks() & 0xFFFFU);
    ping_received = 0U;
    ping_active = 2U; /* Reserved while ARP resolution and TX are in progress. */
    uint16_t identifier = ping_identifier;
    uint16_t sequence = ping_sequence;
    spinlock_unlock_irqrestore(&ping_lock, flags);

    uint8_t packet[20] = {0};
    packet[0] = 8U;
    write_be16(packet + 4U, identifier);
    write_be16(packet + 6U, sequence);
    static const uint8_t marker[] = "VANTAOS-PING";
    for (size_t i = 0; i < sizeof(marker) - 1U; ++i)
        packet[8U + i] = marker[i];
    write_be16(packet + 2U, checksum(packet, sizeof(packet)));

    int sent = net_ipv4_send(device, destination, IPV4_PROTOCOL_ICMP,
                             packet, sizeof(packet));
    flags = spinlock_lock_irqsave(&ping_lock);
    ping_received = 0U;
    ping_active = sent ? 1U : 0U;
    spinlock_unlock_irqrestore(&ping_lock, flags);
    uint32_t start = timer_get_ticks();
    uint32_t limit = timeout_ms / 10U;
    while (sent && (uint32_t)(timer_get_ticks() - start) < limit) {
        (void)net_poll(16U);
        flags = spinlock_lock_irqsave(&ping_lock);
        int received = ping_received != 0U;
        spinlock_unlock_irqrestore(&ping_lock, flags);
        if (received) break;
        __asm__ volatile ("pause");
    }

    uint32_t elapsed = (timer_get_ticks() - start) * 10U;
    flags = spinlock_lock_irqsave(&ping_lock);
    int success = sent && ping_received != 0U;
    ping_active = 0U;
    ping_received = 0U;
    spinlock_unlock_irqrestore(&ping_lock, flags);
    if (success && elapsed_ms) *elapsed_ms = elapsed;
    return success;
}
