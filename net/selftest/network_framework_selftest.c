/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "net_selftest.h"
#include "net.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

#ifdef VANTAOS_SELF_TEST
static uint32_t mock_tx_count;
static uint16_t received_type;
static size_t received_header,received_length;
static int attempt_unregister_during_dispatch;
static int unregister_during_dispatch_result;
static int mock_transmit(net_device_t* d,const uint8_t* frame,size_t len) {
    (void)d;if(!frame||len<NET_ETHERNET_HEADER_SIZE)return 0;++mock_tx_count;return 1;
}
static void mock_handler(net_device_t* d,const uint8_t* frame,size_t len,uint16_t type,size_t header,void* ctx) {
    (void)frame;(void)ctx;received_type=type;received_header=header;received_length=len;
    if(attempt_unregister_during_dispatch)unregister_during_dispatch_result=net_device_unregister(d);
}
void network_framework_selftest(void) {
    static const net_device_ops_t ops={.transmit=mock_transmit};
    net_device_t d={.name="nettest0",.mac={0x02,0,0,0,0,1},.mtu=NET_MTU_DEFAULT,.link_up=1,.ops=&ops};
    uint8_t frame[NET_ETHERNET_HEADER_SIZE+4]={0x02,0,0,0,0,2,0x02,0,0,0,0,1,0x88,0xB5,0x56,0x41,0x4E,0x54};
    uint8_t vlan[NET_ETHERNET_VLAN_HEADER_SIZE+4]={0x02,0,0,0,0,2,0x02,0,0,0,0,1,0x81,0x00,0,1,0x88,0xB5,0x56,0x41,0x4E,0x54};
    uint8_t short_frame[13]={0};
    mock_tx_count=0;received_type=0;received_header=received_length=0;
    int ok=net_device_register(&d);
    ok=ok&&net_device_count()>0&&net_device_get(0)!=0;
    ok=ok&&net_device_configure_ipv4(&d,0xC0A80102U,0xFFFFFF00U,0xC0A80101U);
    uint32_t address=0,mask=0,gateway=0;
    ok=ok&&net_device_get_ipv4(&d,&address,&mask,&gateway);
    ok=ok&&address==0xC0A80102U&&mask==0xFFFFFF00U&&gateway==0xC0A80101U;
    ok=!net_device_configure_ipv4(&d,0xC0A80102U,0xFF00FF00U,0xC0A80101U)&&ok;
    ok=!net_device_configure_ipv4(&d,0xC0A80100U,0xFFFFFF00U,0xC0A80101U)&&ok;
    ok=!net_device_configure_ipv4(&d,0xC0A801FFU,0xFFFFFF00U,0xC0A80101U)&&ok;
    ok=!net_device_configure_ipv4(&d,0xC0A80102U,0xFFFFFF00U,0xC0A80102U)&&ok;
    ok=net_device_get_ipv4(&d,&address,&mask,&gateway)&&ok;
    ok=address==0xC0A80102U&&mask==0xFFFFFF00U&&gateway==0xC0A80101U&&ok;
    ok=ok&&net_ethernet_handler_register(0x88B5U,mock_handler,0);
    ok=ok&&net_device_transmit(&d,frame,sizeof(frame))&&mock_tx_count==1;
    ok=net_device_set_link(&d,0)&&ok;
    ok=!net_device_transmit(&d,frame,sizeof(frame))&&ok;
    ok=net_device_set_link(&d,1)&&ok;
    ok=net_ethernet_dispatch(&d,frame,sizeof(frame))&&ok;
    ok=received_type==0x88B5U&&received_header==NET_ETHERNET_HEADER_SIZE&&received_length==sizeof(frame)&&ok;
    ok=net_ethernet_dispatch(&d,vlan,sizeof(vlan))&&ok;
    ok=received_type==0x88B5U&&received_header==NET_ETHERNET_VLAN_HEADER_SIZE&&ok;
    ok=!net_ethernet_dispatch(&d,short_frame,sizeof(short_frame))&&ok;
    attempt_unregister_during_dispatch=1;
    unregister_during_dispatch_result=1;
    ok=net_ethernet_dispatch(&d,frame,sizeof(frame))&&ok;
    ok=!unregister_during_dispatch_result&&ok;
    attempt_unregister_during_dispatch=0;
    ok=net_ethernet_handler_unregister(0x88B5U,mock_handler)&&ok;
    ok=net_device_unregister(&d)&&ok;
    serial_write(ok?"VANTAOS_NETWORK_FRAMEWORK_SELFTEST_OK\n":"VANTAOS_NETWORK_FRAMEWORK_SELFTEST_FAIL\n");
}
#endif /* VANTAOS_SELF_TEST */
