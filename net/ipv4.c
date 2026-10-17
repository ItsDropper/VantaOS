#include "net.h"
#include "serial.h"
#include <stddef.h>
#include <stdint.h>
#define IPV4_ETHERTYPE 0x0800U
#define IPV4_PROTOCOL_ICMP 1U
#define IPV4_PROTOCOL_UDP 17U
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
    if(ip[9]!=IPV4_PROTOCOL_ICMP&&ip[9]!=IPV4_PROTOCOL_UDP)return;
    uint32_t local_ip=0,local_mask=0,source_ip=read_be32(ip+12),dest_ip=read_be32(ip+16);
    if(!net_device_get_ipv4(d,&local_ip,&local_mask,0)||dest_ip!=local_ip||
       !unicast_ipv4(source_ip)||source_ip==local_ip||ip[8]<=1U||
       !valid_source_mac(frame+6)||
       ((source_ip&local_mask)==(local_ip&local_mask)&&
        (((source_ip&~local_mask)==0U)||((source_ip&~local_mask)==~local_mask))))return;
    size_t icmp_length=(size_t)total-ihl;
    const uint8_t* icmp=ip+ihl;
    if(ip[9]==IPV4_PROTOCOL_UDP){
        net_udp_receive_ipv4(d,ip,ihl,total);
        return;
    }
    if(icmp_length<8U||icmp[0]!=8U||icmp[1]!=0U||checksum(icmp,icmp_length)!=0U)return;
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
