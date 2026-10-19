/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "net.h"
#include "dma.h"
#include "pci.h"
#include "serial.h"
#include "spinlock.h"
#include <stddef.h>
#include <stdint.h>
#define VNET_VENDOR 0x1AF4U
#define VNET_LEGACY_ID 0x1000U
#define VNET_MAX_QUEUE 256U
#define VNET_MAX_RING_PAGES 4U
#define VNET_BUFFER_SIZE 2048U
#define VNET_PAGE_SIZE 4096U
#define VNET_WAIT_LIMIT 10000000U
#define VNET_F_MAC (1U << 5)
#define VNET_F_STATUS (1U << 16)
#define VNET_STATUS_ACK 1U
#define VNET_STATUS_DRIVER 2U
#define VNET_STATUS_DRIVER_OK 4U
#define VNET_NET_HDR_SIZE 10U
typedef struct __attribute__((packed)){uint64_t address;uint32_t length;uint16_t flags,next;} vnet_desc_t;
typedef struct __attribute__((packed)){uint16_t flags,index;uint16_t ring[VNET_MAX_QUEUE];} vnet_avail_t;
typedef struct __attribute__((packed)){uint32_t id,length;} vnet_used_elem_t;
typedef struct __attribute__((packed)){uint16_t flags,index;vnet_used_elem_t ring[VNET_MAX_QUEUE];} vnet_used_t;
typedef struct {
 dma_page_t ring_pages[VNET_MAX_RING_PAGES];uint32_t ring_page_count;dma_page_t buffers[VNET_MAX_QUEUE];
 uint16_t size,avail_index,last_used;vnet_desc_t *descriptors;vnet_avail_t *available;vnet_used_t *used;
} vnet_queue_t;
typedef struct {
 const struct pci_device *pci;uint16_t io_base;uint32_t host_features;uint8_t has_status,ready,registered;
 net_device_t netdev;vnet_queue_t rx,tx;spinlock_t tx_lock;
} vnet_state_t;
static vnet_state_t state;
static int vnet_poll_device(void);
static net_pci_probe_result_t vnet_probe(const struct pci_device*);
static const net_pci_id_t vnet_ids[]={{VNET_VENDOR,VNET_LEGACY_ID,0x02U,0x00U,0x00U}};
static int vnet_transmit(net_device_t*,const uint8_t*,size_t);
static const net_device_ops_t vnet_net_ops={.transmit=vnet_transmit};
static const net_pci_driver_t vnet_driver={.name="virtio-net-legacy",.ids=vnet_ids,.id_count=1,.probe=vnet_probe,.poll=vnet_poll_device};
static inline void out8(uint16_t p,uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline void out16(uint16_t p,uint16_t v){__asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p));}
static inline void out32(uint16_t p,uint32_t v){__asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));}
static inline uint8_t in8(uint16_t p){uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline uint16_t in16(uint16_t p){uint16_t v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}
static inline uint32_t in32(uint16_t p){uint32_t v;__asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p));return v;}
static size_t align_page(size_t n){return(n+VNET_PAGE_SIZE-1U)&~(size_t)(VNET_PAGE_SIZE-1U);}
static size_t queue_bytes(uint16_t n){return align_page((size_t)n*sizeof(vnet_desc_t)+4U+(size_t)n*2U)+4U+(size_t)n*sizeof(vnet_used_elem_t);}
static int queue_allocate(vnet_queue_t*q,uint16_t n){
 if(!q||n<8U||n>VNET_MAX_QUEUE||(n&(n-1U)))return 0;
 size_t bytes=queue_bytes(n);q->ring_page_count=(uint32_t)((bytes+VNET_PAGE_SIZE-1U)/VNET_PAGE_SIZE);
 if(!q->ring_page_count||q->ring_page_count>VNET_MAX_RING_PAGES||!dma_alloc_contiguous(q->ring_pages,q->ring_page_count))return 0;
 q->size=n;uint8_t*b=(uint8_t*)q->ring_pages[0].virtual_address;
 q->descriptors=(vnet_desc_t*)b;q->available=(vnet_avail_t*)(b+(size_t)n*sizeof(vnet_desc_t));
 q->used=(vnet_used_t*)(b+align_page((size_t)n*sizeof(vnet_desc_t)+4U+(size_t)n*2U));
 for(uint16_t i=0;i<n;i++)if(!dma_alloc_page(&q->buffers[i]))return 0;
 return 1;
}
static void queue_release(vnet_queue_t*q){
 if(!q)return;for(uint32_t i=0;i<VNET_MAX_QUEUE;i++)dma_free_page(&q->buffers[i]);
 dma_free_contiguous(q->ring_pages,q->ring_page_count);uint8_t*z=(uint8_t*)q;for(size_t i=0;i<sizeof(*q);i++)z[i]=0;
}
static void queue_publish(uint16_t base,uint16_t index,vnet_queue_t*q){out16(base+0x0EU,index);out32(base+0x08U,(uint32_t)(q->ring_pages[0].physical_address>>12));}
static void queue_notify(uint16_t base,uint16_t index){out16(base+0x10U,index);}
static int vnet_transmit(net_device_t*d,const uint8_t*f,size_t n){
 if(d!=&state.netdev||!state.ready||!f||n<NET_ETHERNET_HEADER_SIZE||n>NET_FRAME_MAX_SIZE||n+VNET_NET_HDR_SIZE>VNET_BUFFER_SIZE)return 0;
 spinlock_lock(&state.tx_lock);vnet_queue_t*q=&state.tx;uint16_t slot=(uint16_t)(q->avail_index%q->size),target=(uint16_t)(q->avail_index+1U);
 uint8_t*b=(uint8_t*)q->buffers[slot].virtual_address;for(uint32_t i=0;i<VNET_NET_HDR_SIZE;i++)b[i]=0;for(size_t i=0;i<n;i++)b[VNET_NET_HDR_SIZE+i]=f[i];
 q->descriptors[slot].address=q->buffers[slot].physical_address;q->descriptors[slot].length=(uint32_t)(VNET_NET_HDR_SIZE+n);q->descriptors[slot].flags=0;q->descriptors[slot].next=0;
 q->available->ring[q->avail_index%q->size]=slot;__asm__ volatile("":::"memory");q->avail_index=target;q->available->index=target;__asm__ volatile("":::"memory");queue_notify(state.io_base,1U);
 int done=0;for(uint32_t w=0;w<VNET_WAIT_LIMIT;w++){__asm__ volatile("":::"memory");if((uint16_t)(q->used->index-target)<0x8000U){q->last_used=q->used->index;done=1;break;}__asm__ volatile("pause");}
 spinlock_unlock(&state.tx_lock);return done;
}
static int vnet_poll_device(void){
 if(!state.ready)return 0;vnet_queue_t*q=&state.rx;int count=0;
 for(uint32_t budget=0;budget<64U;budget++){
  __asm__ volatile("":::"memory");if(q->last_used==q->used->index)break;
  vnet_used_elem_t e=q->used->ring[q->last_used%q->size];++q->last_used;if(e.id>=q->size)continue;
  uint16_t id=(uint16_t)e.id;uint8_t*b=(uint8_t*)q->buffers[id].virtual_address;
  if(e.length>=VNET_NET_HDR_SIZE+NET_ETHERNET_HEADER_SIZE&&e.length<=VNET_BUFFER_SIZE){size_t n=e.length-VNET_NET_HDR_SIZE;if(n<=NET_FRAME_MAX_SIZE)(void)net_receive_frame(&state.netdev,b+VNET_NET_HDR_SIZE,n);}
  q->descriptors[id].address=q->buffers[id].physical_address;q->descriptors[id].length=VNET_BUFFER_SIZE;q->descriptors[id].flags=0;q->descriptors[id].next=0;
  q->available->ring[q->avail_index%q->size]=id;++q->avail_index;q->available->index=q->avail_index;queue_notify(state.io_base,0U);++count;
 }
 if(state.has_status){int up=(in16(state.io_base+0x1AU)&1U)!=0U;if((int)state.netdev.link_up!=up)(void)net_device_set_link(&state.netdev,up);}
 return count;
}
static void vnet_release(void){
 if(state.registered){(void)net_device_unregister(&state.netdev);state.registered=0;}state.ready=0;
 queue_release(&state.rx);queue_release(&state.tx);state.io_base=0;state.pci=0;
}
static net_pci_probe_result_t vnet_probe(const struct pci_device*pci){
 if(!pci||state.ready)return NET_PCI_PROBE_NOT_MINE;
 uint64_t bar=0;int io=0;if(!pci_get_bar(pci,0,&bar,&io,0)||!io||!bar||bar>0xFFFFU||bar+0x1BU>0xFFFFU)return NET_PCI_PROBE_NOT_MINE;
 if(!pci_enable_io_space(pci))return NET_PCI_PROBE_FAILED;
 state.pci=pci;state.io_base=(uint16_t)bar;out8(state.io_base+0x12U,0U);
 for(uint32_t i=0;i<100000U&&in8(state.io_base+0x12U)!=0U;i++)__asm__ volatile("pause");
 out8(state.io_base+0x12U,VNET_STATUS_ACK);out8(state.io_base+0x12U,VNET_STATUS_ACK|VNET_STATUS_DRIVER);
 state.host_features=in32(state.io_base);if(!(state.host_features&VNET_F_MAC))goto fail;
 state.has_status=(state.host_features&VNET_F_STATUS)!=0U;out32(state.io_base+0x04U,VNET_F_MAC|(state.has_status?VNET_F_STATUS:0U));
 out16(state.io_base+0x0EU,0U);uint16_t rn=in16(state.io_base+0x0CU);out16(state.io_base+0x0EU,1U);uint16_t tn=in16(state.io_base+0x0CU);
 if(!queue_allocate(&state.rx,rn)||!queue_allocate(&state.tx,tn))goto fail;
 for(uint16_t i=0;i<state.rx.size;i++){state.rx.descriptors[i].address=state.rx.buffers[i].physical_address;state.rx.descriptors[i].length=VNET_BUFFER_SIZE;state.rx.descriptors[i].flags=0;state.rx.descriptors[i].next=0;state.rx.available->ring[i]=i;}
 state.rx.avail_index=state.rx.size;state.rx.available->index=state.rx.avail_index;queue_publish(state.io_base,0U,&state.rx);queue_publish(state.io_base,1U,&state.tx);
 uint8_t mac[6],any=0;for(uint32_t i=0;i<6U;i++){mac[i]=in8(state.io_base+0x14U+(uint16_t)i);any|=mac[i];}if(!any||(mac[0]&1U))goto fail;
 if(!pci_enable_bus_mastering(pci))goto fail;
 out8(state.io_base+0x12U,VNET_STATUS_ACK|VNET_STATUS_DRIVER|VNET_STATUS_DRIVER_OK);
 uint8_t *ndzero=(uint8_t*)&state.netdev;for(size_t i=0;i<sizeof(state.netdev);i++)ndzero[i]=0;
 state.netdev.name[0]='v';state.netdev.name[1]='n';state.netdev.name[2]='e';state.netdev.name[3]='t';state.netdev.name[4]='0';
 for(uint32_t i=0;i<6U;i++)state.netdev.mac[i]=mac[i];
 state.netdev.mtu=NET_MTU_DEFAULT;
 state.netdev.link_up=state.has_status?((in16(state.io_base+0x1AU)&1U)!=0U):1U;
 state.netdev.ops=&vnet_net_ops;state.netdev.driver_data=&state;
 spinlock_init(&state.tx_lock);if(!net_device_register(&state.netdev))goto fail;state.registered=1;state.ready=1;queue_notify(state.io_base,0U);
#ifdef VANTAOS_SELF_TEST
 uint8_t probe[18]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],0x88,0xB5,0x56,0x41,0x4E,0x54};
 if(net_device_transmit(&state.netdev,probe,sizeof(probe)))serial_write("VANTAOS_VIRTIO_NET_TX_SELFTEST_OK\n");
 else serial_write("VANTAOS_VIRTIO_NET_TX_SELFTEST_FAIL\n");
#endif
 serial_write("VANTAOS_VIRTIO_NET_INITIALIZED\n");return NET_PCI_PROBE_BOUND;
fail: out8(state.io_base+0x12U,0U);
 for(uint32_t i=0;i<100000U&&in8(state.io_base+0x12U)!=0U;i++)__asm__ volatile("pause");
 vnet_release();return NET_PCI_PROBE_FAILED;
}
int virtio_net_initialize(void){uint8_t*p=(uint8_t*)&state;for(size_t i=0;i<sizeof(state);i++)p[i]=0;spinlock_init(&state.tx_lock);return net_pci_driver_register(&vnet_driver);}
