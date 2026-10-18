#include "net.h"
#include "timer.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define BOOTP_OFFSET 42U
#define BOOTP_FIXED 236U
#define COOKIE_OFFSET 236U
#define OPTIONS_OFFSET 240U

static net_udp_handler_fn udp_handler;
static void *udp_context;
static uint16_t udp_port;
static uint32_t ticks;
static net_device_t device;
static net_device_t second_device;
static net_device_t *active_device;
static uint32_t tx_counts[2];
static uint8_t tx_frame[600];
static size_t tx_length;
static uint8_t last_message_type;
static int test_mode;
static int offer_sent;
static int ack_sent;

uint32_t timer_get_ticks(void) { return ticks++; }

static void put32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v; }
static uint32_t get32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }

int net_udp_handler_register(uint16_t port, net_udp_handler_fn fn, void *context)
{
    if (!fn || udp_handler) return 0;
    udp_port=port;udp_handler=fn;udp_context=context;return 1;
}
int net_udp_handler_unregister(uint16_t port, net_udp_handler_fn fn, void *context)
{
    if(port!=udp_port||fn!=udp_handler||context!=udp_context)return 0;
    udp_handler=0;udp_context=0;udp_port=0;return 1;
}
int net_arp_resolve(net_device_t *d,uint32_t target,uint8_t mac[6],uint32_t timeout)
{ (void)d;(void)target;(void)mac;(void)timeout;return 0; }
int net_device_transmit(net_device_t *d,const void *frame,size_t length)
{
    if((d!=&device&&d!=&second_device)||!frame||length>sizeof(tx_frame))return 0;
    active_device=d;
    if(d==&device)++tx_counts[0];else ++tx_counts[1];
    const uint8_t *p=(const uint8_t*)frame;
    for (size_t i = 0; i < length; i++) {
        tx_frame[i] = p[i];
    }
    tx_length = length;
    if(length>BOOTP_OFFSET+OPTIONS_OFFSET+2U)
        last_message_type=p[BOOTP_OFFSET+OPTIONS_OFFSET+2U];
    return 1;
}
int net_device_configure_ipv4(net_device_t *d,uint32_t address,uint32_t mask,uint32_t gateway)
{
    if (d != &device && d != &second_device) return 0;
    d->ipv4_address = address;
    d->ipv4_netmask = mask;
    d->ipv4_gateway = gateway;
    d->ipv4_configured = 1;
    return 1;
}
int net_device_clear_ipv4(net_device_t *d)
{ if(d!=&device&&d!=&second_device)return 0;d->ipv4_address=d->ipv4_netmask=d->ipv4_gateway=d->ipv4_dns=0;d->ipv4_configured=0;return 1; }

static void add_option(uint8_t *p,size_t *n,uint8_t code,const uint8_t *value,uint8_t length)
{
    p[(*n)++]=code;p[(*n)++]=length;
    for(uint8_t i=0;i<length;i++)p[(*n)++]=value[i];
}
static void inject_reply(uint8_t kind,int bad_xid,int malformed_mask)
{
    uint8_t p[320]={0};
    p[0]=2;p[1]=1;p[2]=6;
    uint32_t xid=get32(tx_frame+BOOTP_OFFSET+4U);
    put32(p+4U,bad_xid?xid^0x01020304U:xid);
    put32(p+16U,0xC000020AU);
    for(size_t i=0;i<6U;i++)p[28U+i]=active_device->mac[i];
    put32(p+COOKIE_OFFSET,0x63825363U);
    size_t n=OPTIONS_OFFSET;
    uint8_t type=kind;add_option(p,&n,53U,&type,1U);
    const uint8_t server[4]={192,0,2,1};add_option(p,&n,54U,server,4U);
    const uint8_t mask_bad[3]={255,255,255};
    const uint8_t mask[4]={255,255,255,0};add_option(p,&n,1U,malformed_mask?mask_bad:mask,malformed_mask?3U:4U);
    const uint8_t gateway[4]={192,0,2,1};add_option(p,&n,3U,gateway,4U);
    const uint8_t dns[4]={1,1,1,1};add_option(p,&n,6U,dns,4U);
    const uint8_t lease[4]={0,0,0,(uint8_t)(test_mode==4?10:0)};
    const uint8_t normal_lease[4]={0,0,14,16};
    add_option(p,&n,51U,test_mode==4?lease:normal_lease,4U);
    p[n++]=255U;
    if(udp_handler)udp_handler(active_device,0xC0000201U,67U,68U,p,n,udp_context);
}
size_t net_poll(size_t packet_budget)
{
    (void)packet_budget;
    if(last_message_type==1U&&!offer_sent){
        offer_sent=1;
        if(test_mode==2){inject_reply(6U,0,0);return 1U;}
        if(test_mode==3)inject_reply(2U,1,0);
        inject_reply(2U,0,test_mode==1);
        return 1U;
    }
    if(last_message_type==3U&&!ack_sent){ack_sent=1;inject_reply(5U,0,0);return 1U;}
    return 0U;
}

static int run_case(int mode,int expected)
{
    device=(net_device_t){0};device.name[0]='n';device.name[1]='0';
    device.mac[0]=0x02;device.mac[5]=0x42;device.mtu=1500;device.link_up=1;
    active_device=&device;tx_counts[0]=0;
    test_mode=mode;offer_sent=ack_sent=0;last_message_type=0;tx_length=0;ticks=0;
    uint32_t dns=0xDEADBEEFU,lease=0;
    int result=net_dhcp_configure(&device,1000U,&dns,&lease);
    if(result!=expected)return 0;
    if(expected){
        if(!device.ipv4_configured||device.ipv4_address!=0xC000020AU||
           device.ipv4_netmask!=0xFFFFFF00U||device.ipv4_gateway!=0xC0000201U||
           dns!=0x01010101U||lease!=3600U)return 0;
    }else if(device.ipv4_configured||dns!=0xDEADBEEFU)return 0;
    return tx_length!=0U&&udp_handler!=0&&udp_context==0&&udp_port==68U;
}


static int run_dual_interface_renewal(void)
{
    device=(net_device_t){0}; second_device=(net_device_t){0};
    device.name[0]='n';device.name[1]='0';device.mac[0]=0x02;device.mac[5]=0x42;device.mtu=1500;device.link_up=1;
    second_device.name[0]='n';second_device.name[1]='1';second_device.mac[0]=0x02;second_device.mac[5]=0x43;second_device.mtu=1500;second_device.link_up=1;
    tx_counts[0]=tx_counts[1]=0;ticks=0;test_mode=4;
    offer_sent=ack_sent=0;last_message_type=0;tx_length=0;active_device=&device;
    uint32_t dns_a=0,lease_a=0,dns_b=0,lease_b=0;
    if(!net_dhcp_configure(&device,1000U,&dns_a,&lease_a)||lease_a!=10U)return 0;
    offer_sent=ack_sent=0;last_message_type=0;tx_length=0;active_device=&second_device;
    if(!net_dhcp_configure(&second_device,1000U,&dns_b,&lease_b)||lease_b!=10U)return 0;
    uint32_t initial_a=tx_counts[0],initial_b=tx_counts[1];
    for(uint32_t i=0;i<2200U;i++){
        net_dhcp_poll();
        ack_sent=0;
        if(last_message_type==3U)(void)net_poll(16U);
    }
    return device.ipv4_configured&&second_device.ipv4_configured&&
           tx_counts[0]>initial_a&&tx_counts[1]>initial_b;
}

int main(void)
{
    if(!run_case(0,1)){fputs("DHCP DORA/lease configuration failed\n",stderr);return 1;}
    if(!run_case(1,0)){fputs("DHCP accepted a malformed subnet-mask option\n",stderr);return 2;}
    if(!run_case(2,0)){fputs("DHCP NAK did not fail closed\n",stderr);return 3;}
    if(!run_case(3,1)){fputs("DHCP accepted wrong transaction ID or rejected subsequent valid lease\n",stderr);return 4;}
    if(!run_dual_interface_renewal()){fputs("DHCP did not maintain independent leases and renewal state for two interfaces\\n",stderr);return 5;}
    puts("DHCP DORA, malformed replies, transaction IDs, and independent multi-interface renewal passed");
    return 0;
}
