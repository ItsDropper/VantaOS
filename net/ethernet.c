#include "net.h"
extern int net_internal_pop_rx(net_device_t**,uint8_t*,size_t,size_t*);
extern int net_internal_dispatch_ethernet(net_device_t*,const uint8_t*,size_t,uint16_t,size_t);
static uint16_t be16(const uint8_t* p){return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static int decode(const uint8_t* f,size_t n,uint16_t* type,size_t* h) {
    if(!f||!type||!h||n<NET_ETHERNET_HEADER_SIZE)return 0;
    uint16_t t=be16(f+12);size_t header=NET_ETHERNET_HEADER_SIZE;
    if(t==0x8100U||t==0x88A8U){if(n<NET_ETHERNET_VLAN_HEADER_SIZE)return 0;t=be16(f+16);header=NET_ETHERNET_VLAN_HEADER_SIZE;
        if(t==0x8100U||t==0x88A8U)return 0;}
    if(t<0x0600U)return 0;
    *type=t;*h=header;return 1;
}
size_t net_poll(size_t budget) {
    (void)net_pci_poll_devices();
    uint8_t frame[NET_FRAME_MAX_SIZE];size_t done=0;
    while(done<budget){net_device_t* d=0;size_t len=0;
        if(!net_internal_pop_rx(&d,frame,sizeof(frame),&len))break;
        uint16_t type=0;size_t header=0;
        if(decode(frame,len,&type,&header))(void)net_internal_dispatch_ethernet(d,frame,len,type,header);
        ++done;
    }
    return done;
}
