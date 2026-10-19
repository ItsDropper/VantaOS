/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "net.h"
#include "spinlock.h"
typedef struct { net_device_t* device; net_device_stats_t stats; } device_slot_t;
typedef struct { net_device_t* device; uint16_t length; uint8_t frame[NET_FRAME_MAX_SIZE]; } rx_entry_t;
typedef struct { uint16_t type; net_ethernet_handler_fn fn; void* context; } eth_handler_t;
static device_slot_t devices[NET_DEVICE_MAX];
static uint32_t device_count_used;
static rx_entry_t rxq[NET_RX_QUEUE_CAPACITY];
static uint32_t rx_head,rx_tail,rx_used;
static eth_handler_t handlers[NET_ETH_HANDLER_MAX];
static uint32_t handlers_used;
static spinlock_t lock=SPINLOCK_INITIALIZER;
static int initialized;
static device_slot_t* find_locked(net_device_t* d) {
    for(uint32_t i=0;i<device_count_used;i++) if(devices[i].device==d)return &devices[i];
    return 0;
}
static int frame_length_valid(const uint8_t* frame,size_t len,uint16_t mtu) {
    if(!frame||len<NET_ETHERNET_HEADER_SIZE||len>NET_FRAME_MAX_SIZE)return 0;
    uint16_t type=(uint16_t)(((uint16_t)frame[12]<<8)|frame[13]);
    size_t header=(type==0x8100U||type==0x88A8U)?NET_ETHERNET_VLAN_HEADER_SIZE:NET_ETHERNET_HEADER_SIZE;
    return len<=(size_t)mtu+header;
}
void net_initialize(void) {
    uint64_t f=spinlock_lock_irqsave(&lock);
    for(uint32_t i=0;i<NET_DEVICE_MAX;i++){devices[i].device=0;devices[i].stats=(net_device_stats_t){0};}
    device_count_used=rx_head=rx_tail=rx_used=handlers_used=0; initialized=1;
    spinlock_unlock_irqrestore(&lock,f);
}
static int valid_mac(const uint8_t* m) {
    uint8_t any=0,all_ff=1;
    for(uint32_t i=0;i<6;i++){any|=m[i];if(m[i]!=0xFFU)all_ff=0;}
    return any&&!all_ff&&!(m[0]&1U);
}
static int valid_name(const char* n) {
    if(!n||!n[0])return 0;
    for(uint32_t i=0;i<NET_DEVICE_NAME_MAX;i++)if(!n[i])return 1;
    return 0;
}
int net_device_register(net_device_t* d) {
    if(!d||!valid_name(d->name)||!valid_mac(d->mac)||d->mtu<NET_MTU_MIN||
       d->mtu>NET_MTU_DEFAULT||!d->ops||!d->ops->transmit)return 0;
    uint64_t f=spinlock_lock_irqsave(&lock);
    if(!initialized||find_locked(d)||device_count_used==NET_DEVICE_MAX){spinlock_unlock_irqrestore(&lock,f);return 0;}
    devices[device_count_used].device=d;devices[device_count_used].stats=(net_device_stats_t){0};++device_count_used;
    spinlock_unlock_irqrestore(&lock,f);return 1;
}
int net_device_unregister(net_device_t* d) {
    if(!d)return 0;
    uint64_t f=spinlock_lock_irqsave(&lock);uint32_t at=device_count_used;
    for(uint32_t i=0;i<device_count_used;i++)if(devices[i].device==d){at=i;break;}
    if(at==device_count_used){spinlock_unlock_irqrestore(&lock,f);return 0;}
    for(uint32_t i=0,pos=rx_head;i<rx_used;i++,pos=(pos+1U)%NET_RX_QUEUE_CAPACITY)
        if(rxq[pos].device==d){spinlock_unlock_irqrestore(&lock,f);return 0;}
    for(uint32_t i=at;i+1U<device_count_used;i++)devices[i]=devices[i+1U];
    --device_count_used;devices[device_count_used].device=0;devices[device_count_used].stats=(net_device_stats_t){0};
    d->ipv4_address=0;d->ipv4_netmask=0;d->ipv4_gateway=0;d->ipv4_dns=0;d->ipv4_configured=0;
    spinlock_unlock_irqrestore(&lock,f);return 1;
}
int net_device_set_link(net_device_t* d,int up) {
    uint64_t f=spinlock_lock_irqsave(&lock);if(!find_locked(d)){spinlock_unlock_irqrestore(&lock,f);return 0;}
    d->link_up=up?1U:0U;spinlock_unlock_irqrestore(&lock,f);return 1;
}

static int ipv4_unicast_host(uint32_t address)
{
    uint8_t first=(uint8_t)(address>>24);
    return address!=0U&&address!=0xFFFFFFFFU&&first!=0U&&first!=127U&&first<224U;
}
int net_device_configure_ipv4(net_device_t* d, uint32_t address,
                              uint32_t mask, uint32_t gateway)
{
    uint32_t inverse = ~mask;
    if (!d || !ipv4_unicast_host(address) || mask == 0U ||
        (inverse & (inverse + 1U)) != 0U)
        return 0;

    /* For ordinary subnets, neither the network nor directed-broadcast
     * address is a valid host address. /31 point-to-point and /32 host
     * routes are left available for their defined special use cases. */
    if (inverse > 1U &&
        ((address & inverse) == 0U || (address & inverse) == inverse))
        return 0;

    if (gateway &&
        (!ipv4_unicast_host(gateway) || gateway == address ||
         (gateway & mask) != (address & mask) ||
         (inverse > 1U &&
          ((gateway & inverse) == 0U || (gateway & inverse) == inverse))))
        return 0;

    uint64_t f = spinlock_lock_irqsave(&lock);
    device_slot_t* s = find_locked(d);
    if (!s) {
        spinlock_unlock_irqrestore(&lock, f);
        return 0;
    }
    d->ipv4_address = address;
    d->ipv4_netmask = mask;
    d->ipv4_gateway = gateway;
    d->ipv4_dns = 0U;
    d->ipv4_configured = 1U;
    spinlock_unlock_irqrestore(&lock, f);
    return 1;
}
int net_device_clear_ipv4(net_device_t* d)
{
    if(!d)return 0;
    uint64_t f=spinlock_lock_irqsave(&lock);device_slot_t* s=find_locked(d);
    if(!s){spinlock_unlock_irqrestore(&lock,f);return 0;}
    d->ipv4_address=0;d->ipv4_netmask=0;d->ipv4_gateway=0;d->ipv4_dns=0;d->ipv4_configured=0;
    spinlock_unlock_irqrestore(&lock,f);return 1;
}
int net_device_get_ipv4(net_device_t* d,uint32_t* address,uint32_t* mask,uint32_t* gateway)
{
    if(!d)return 0;
    uint64_t f=spinlock_lock_irqsave(&lock);device_slot_t* s=find_locked(d);
    if(!s||!d->ipv4_configured){spinlock_unlock_irqrestore(&lock,f);return 0;}
    if(address)*address=d->ipv4_address;
    if(mask)*mask=d->ipv4_netmask;
    if(gateway)*gateway=d->ipv4_gateway;
    spinlock_unlock_irqrestore(&lock,f);return 1;
}

uint32_t net_device_count(void) {uint64_t f=spinlock_lock_irqsave(&lock);uint32_t n=device_count_used;spinlock_unlock_irqrestore(&lock,f);return n;}
net_device_t* net_device_get(uint32_t i) {uint64_t f=spinlock_lock_irqsave(&lock);net_device_t* d=i<device_count_used?devices[i].device:0;spinlock_unlock_irqrestore(&lock,f);return d;}
int net_device_get_stats(net_device_t* d,net_device_stats_t* out) {
    if(!d||!out)return 0;
    uint64_t f=spinlock_lock_irqsave(&lock);device_slot_t* s=find_locked(d);
    if(!s){spinlock_unlock_irqrestore(&lock,f);return 0;}
    *out=s->stats;spinlock_unlock_irqrestore(&lock,f);return 1;
}
int net_device_transmit(net_device_t* d,const void* frame,size_t len) {
    if(!d||!frame)return 0;
    if(!frame_length_valid((const uint8_t*)frame,len,d->mtu)) {
        uint64_t f=spinlock_lock_irqsave(&lock);device_slot_t* s=find_locked(d);if(s)++s->stats.tx_errors;
        spinlock_unlock_irqrestore(&lock,f);return 0;
    }
    uint64_t f=spinlock_lock_irqsave(&lock);device_slot_t* s=find_locked(d);
    if(!s||!d->link_up){if(s)++s->stats.tx_errors;spinlock_unlock_irqrestore(&lock,f);return 0;}
    const net_device_ops_t* ops=d->ops;spinlock_unlock_irqrestore(&lock,f);
    int sent=ops->transmit(d,(const uint8_t*)frame,len);
    f=spinlock_lock_irqsave(&lock);s=find_locked(d);int still_registered=s!=0;
    if(s){if(sent){++s->stats.tx_frames;s->stats.tx_bytes+=len;}else ++s->stats.tx_errors;}
    spinlock_unlock_irqrestore(&lock,f);return sent&&still_registered;
}
int net_receive_frame(net_device_t* d,const void* frame,size_t len) {
    if(!d||!frame)return 0;
    uint64_t f=spinlock_lock_irqsave(&lock);device_slot_t* s=find_locked(d);
    if(!s){spinlock_unlock_irqrestore(&lock,f);return 0;}
    if(!frame_length_valid((const uint8_t*)frame,len,d->mtu)){
        ++s->stats.rx_malformed;spinlock_unlock_irqrestore(&lock,f);return 0;}
    if(rx_used==NET_RX_QUEUE_CAPACITY){++s->stats.rx_dropped;spinlock_unlock_irqrestore(&lock,f);return 0;}
    rx_entry_t* e=&rxq[rx_tail];e->device=d;e->length=(uint16_t)len;
    const uint8_t* src=(const uint8_t*)frame;for(size_t i=0;i<len;i++)e->frame[i]=src[i];
    rx_tail=(rx_tail+1U)%NET_RX_QUEUE_CAPACITY;++rx_used;++s->stats.rx_frames;s->stats.rx_bytes+=len;
    spinlock_unlock_irqrestore(&lock,f);return 1;
}
int net_internal_pop_rx(net_device_t** d,uint8_t* frame,size_t cap,size_t* len) {
    if(!d||!frame||!len)return 0;
    uint64_t f=spinlock_lock_irqsave(&lock);
    if(!rx_used||rxq[rx_head].length>cap){spinlock_unlock_irqrestore(&lock,f);return 0;}
    rx_entry_t* e=&rxq[rx_head];*d=e->device;*len=e->length;
    for(size_t i=0;i<e->length;i++)frame[i]=e->frame[i];
    rx_head=(rx_head+1U)%NET_RX_QUEUE_CAPACITY;--rx_used;spinlock_unlock_irqrestore(&lock,f);return 1;
}
int net_internal_dispatch_ethernet(net_device_t* d,const uint8_t* frame,size_t len,uint16_t type,size_t header) {
    eth_handler_t snap[NET_ETH_HANDLER_MAX];uint32_t n=0;uint64_t f=spinlock_lock_irqsave(&lock);
    for(uint32_t i=0;i<handlers_used;i++)if(handlers[i].type==type)snap[n++]=handlers[i];
    spinlock_unlock_irqrestore(&lock,f);
    for(uint32_t i=0;i<n;i++)snap[i].fn(d,frame,len,type,header,snap[i].context);
    return n!=0;
}
int net_ethernet_handler_register(uint16_t type,net_ethernet_handler_fn fn,void* ctx) {
    if(!fn||type==0U||type==0xFFFFU)return 0;
    uint64_t f=spinlock_lock_irqsave(&lock);
    for(uint32_t i=0;i<handlers_used;i++)if(handlers[i].type==type&&handlers[i].fn==fn&&handlers[i].context==ctx){spinlock_unlock_irqrestore(&lock,f);return 0;}
    if(handlers_used==NET_ETH_HANDLER_MAX){spinlock_unlock_irqrestore(&lock,f);return 0;}
    handlers[handlers_used++]=(eth_handler_t){type,fn,ctx};spinlock_unlock_irqrestore(&lock,f);return 1;
}
int net_ethernet_handler_unregister(uint16_t type,net_ethernet_handler_fn fn,void* ctx) {
    uint64_t f=spinlock_lock_irqsave(&lock);uint32_t at=handlers_used;
    for(uint32_t i=0;i<handlers_used;i++)if(handlers[i].type==type&&handlers[i].fn==fn&&handlers[i].context==ctx){at=i;break;}
    if(at==handlers_used){spinlock_unlock_irqrestore(&lock,f);return 0;}
    for(uint32_t i=at;i+1U<handlers_used;i++)handlers[i]=handlers[i+1U];
    --handlers_used;spinlock_unlock_irqrestore(&lock,f);return 1;
}
