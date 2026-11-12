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
static void net_selftest_write_hex64(uint64_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    char output[16];
    for (uint32_t i = 0U; i < 16U; ++i)
        output[i] = digits[(value >> ((15U - i) * 4U)) & 0x0FU];
    serial_write_bytes(output, sizeof(output));
}

static void net_selftest_write_stats(const char* name,
                                     const net_device_stats_t* stats)
{
    serial_write(name);
    serial_write(" tx_frames=");
    net_selftest_write_hex64(stats->tx_frames);
    serial_write(" tx_errors=");
    net_selftest_write_hex64(stats->tx_errors);
    serial_write(" rx_frames=");
    net_selftest_write_hex64(stats->rx_frames);
    serial_write(" rx_dropped=");
    net_selftest_write_hex64(stats->rx_dropped);
    serial_write(" rx_malformed=");
    net_selftest_write_hex64(stats->rx_malformed);
    serial_write("\n");
}

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
    /* Live NICs are registered before this self-test; preserve the baseline. */
    uint32_t baseline_devices=net_device_count();
    const char *failure_stage = NULL;
    int ok=net_device_register(&d);
    ok=ok&&net_device_count()==baseline_devices+1U&&
       net_device_get(baseline_devices)==&d;
    if (!ok) failure_stage = "register";
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
    if (!ok && !failure_stage) failure_stage = "ipv4-validation";
    ok=ok&&net_ethernet_handler_register(0x88B5U,mock_handler,0);
    ok=ok&&net_device_transmit(&d,frame,sizeof(frame))&&mock_tx_count==1;
    if (!ok && !failure_stage) failure_stage = "handler-or-transmit";
    ok=net_device_set_link(&d,0)&&ok;
    ok=!net_device_transmit(&d,frame,sizeof(frame))&&ok;
    ok=net_device_set_link(&d,1)&&ok;
    attempt_unregister_during_dispatch=1;unregister_during_dispatch_result=1;
    ok=net_receive_frame(&d,frame,sizeof(frame))&&ok;
    ok=net_poll(1)==1&&ok;
    attempt_unregister_during_dispatch=0;
    ok=unregister_during_dispatch_result==0&&ok;
    ok=received_type==0x88B5U&&received_header==NET_ETHERNET_HEADER_SIZE&&received_length==sizeof(frame)&&ok;
    if (!ok && !failure_stage) failure_stage = "receive-dispatch";
    ok=net_receive_frame(&d,vlan,sizeof(vlan))&&ok;
    ok=net_poll(1)==1&&ok;
    ok=received_type==0x88B5U&&received_header==NET_ETHERNET_VLAN_HEADER_SIZE&&received_length==sizeof(vlan)&&ok;
    if (!ok && !failure_stage) failure_stage = "vlan-dispatch";
    ok=!net_receive_frame(&d,short_frame,sizeof(short_frame))&&ok;
    /*
     * The RX queue is shared by every live NIC. Earlier boot-time traffic may
     * already occupy slots, so measure this interface's accepted frames rather
     * than assuming the global queue starts empty.
     */
    net_device_stats_t before_queue = {0};
    int before_stats_valid = net_device_get_stats(&d,&before_queue);
    ok=before_stats_valid&&ok;
    uint32_t queued=0U;
    int queue_full=0;
    /*
     * A live interface may be polling concurrently and freeing queue slots.
     * Keep the probe bounded, but allow enough attempts to observe backpressure
     * without assuming the queue remains static for exactly one capacity pass.
     */
    for(uint32_t attempt=0U;
        attempt<NET_RX_QUEUE_CAPACITY*4U && !queue_full;
        ++attempt)
    {
        if(!net_receive_frame(&d,frame,sizeof(frame)))
            queue_full=1;
        else
            ++queued;
    }
    ok=queue_full&&ok; /* bounded queue must reject under sustained pressure */
    if (!ok && !failure_stage) failure_stage = "rx-queue-bounds";
    net_device_stats_t stats = {0};
    int stats_valid = net_device_get_stats(&d,&stats);
    /*
     * Compare deltas: this remains deterministic even when a real interface
     * queued packets before the self-test began.
     */
    int stats_ok = before_stats_valid && stats_valid &&
        mock_tx_count==1U && stats.tx_frames==1U && stats.tx_errors==1U &&
        stats.rx_frames>=before_queue.rx_frames &&
        stats.rx_frames-before_queue.rx_frames==queued &&
        stats.rx_dropped>before_queue.rx_dropped &&
        stats.rx_malformed==before_queue.rx_malformed;
    if (!stats_ok)
    {
        serial_write("VANTAOS_NETWORK_FRAMEWORK_STATS_DIAGNOSTIC queued=");
        net_selftest_write_hex64(queued);
        serial_write(" mock_tx_count=");
        net_selftest_write_hex64(mock_tx_count);
        serial_write("\n");
        net_selftest_write_stats("VANTAOS_NETWORK_FRAMEWORK_STATS_BEFORE",
                                 &before_queue);
        net_selftest_write_stats("VANTAOS_NETWORK_FRAMEWORK_STATS_AFTER",
                                 &stats);
        serial_write("VANTAOS_NETWORK_FRAMEWORK_STATS_VALID before=");
        net_selftest_write_hex64((uint64_t)before_stats_valid);
        serial_write(" after=");
        net_selftest_write_hex64((uint64_t)stats_valid);
        serial_write("\n");
    }
    ok=stats_ok&&ok;
    if (!ok && !failure_stage) failure_stage = "statistics";
    ok=net_ethernet_handler_unregister(0x88B5U,mock_handler,0)&&ok;
    ok=net_device_unregister(&d)&&ok;
    ok=net_device_count()==baseline_devices&&ok;
    if (!ok && !failure_stage) failure_stage = "unregister";
    ok=!net_receive_frame(&d,frame,sizeof(frame))&&ok;
    ok=!net_device_get_ipv4(&d,&address,&mask,&gateway)&&ok;
    if (!ok)
    {
        serial_write("VANTAOS_NETWORK_FRAMEWORK_FAIL_STAGE=");
        serial_write(failure_stage ? failure_stage : "post-unregister");
        serial_write("\n");
    }
    serial_write(ok?"VANTAOS_NETWORK_FRAMEWORK_OK\n":"VANTAOS_NETWORK_FRAMEWORK_FAIL\n");
}
#endif /* VANTAOS_SELF_TEST */
