#include "net.h"
#include "serial.h"
#include <stddef.h>
#include <stdint.h>
#define ARP_ETHERNET_IPV4 0x0800U
#define ARP_REQUEST 1U
#define ARP_REPLY 2U
static uint16_t read_be16(const uint8_t* p){return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static uint32_t read_be32(const uint8_t* p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void write_be16(uint8_t* p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void write_be32(uint8_t* p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static int valid_sender_mac(const uint8_t* m)
{
    uint8_t any=0;for(uint32_t i=0;i<6;i++)any|=m[i];
    return any!=0U&&!(m[0]&1U);
}
static void arp_receive(net_device_t* d,const uint8_t* frame,size_t length,
                        uint16_t type,size_t header,void* context)
{
    (void)type;(void)context;
    if(!d||!frame||header!=NET_ETHERNET_HEADER_SIZE||
       length<header+28U||read_be16(frame+12)!=0x0806U)return;
    const uint8_t* a=frame+header;
    if(read_be16(a)!=1U||read_be16(a+2)!=ARP_ETHERNET_IPV4||
       a[4]!=6U||a[5]!=4U||read_be16(a+6)!=ARP_REQUEST)return;
    const uint8_t* sender_mac=a+8;uint32_t sender_ip=read_be32(a+14);
    uint32_t target_ip=read_be32(a+24),local_ip=0;
    if(!valid_sender_mac(sender_mac)||sender_ip==0U||
       sender_ip==0xFFFFFFFFU||sender_ip>=0xE0000000U||
       sender_mac[0]!=frame[6]||sender_mac[1]!=frame[7]||
       sender_mac[2]!=frame[8]||sender_mac[3]!=frame[9]||
       sender_mac[4]!=frame[10]||sender_mac[5]!=frame[11]||
       !net_device_get_ipv4(d,&local_ip,0,0)||target_ip!=local_ip)return;
    uint8_t out[NET_ETHERNET_HEADER_SIZE+28U]={0};
    for(uint32_t i=0;i<6;i++){out[i]=sender_mac[i];out[6U+i]=d->mac[i];}
    write_be16(out+12,0x0806U);
    uint8_t* reply=out+NET_ETHERNET_HEADER_SIZE;
    write_be16(reply,1U);write_be16(reply+2,ARP_ETHERNET_IPV4);
    reply[4]=6U;reply[5]=4U;write_be16(reply+6,ARP_REPLY);
    for(uint32_t i=0;i<6;i++){reply[8U+i]=d->mac[i];reply[18U+i]=sender_mac[i];}
    write_be32(reply+14,local_ip);write_be32(reply+24,sender_ip);
    int sent=net_device_transmit(d,out,sizeof(out));
#ifdef VANTAOS_SELF_TEST
    if(sent)serial_write("VANTAOS_ARP_REPLY_OK\n");
#else
    (void)sent;
#endif
}
int net_arp_initialize(void)
{
    return net_ethernet_handler_register(0x0806U,arp_receive,0);
}
