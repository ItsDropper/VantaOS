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
#define VNET_DESC_WRITE 2U
typedef struct __attribute__((packed)){uint64_t address;uint32_t length;uint16_t flags,next;} vnet_desc_t;
typedef struct __attribute__((packed)){uint16_t flags,index;uint16_t ring[VNET_MAX_QUEUE];} vnet_avail_t;
typedef struct __attribute__((packed)){uint32_t id,length;} vnet_used_elem_t;
typedef struct __attribute__((packed)){uint16_t flags,index;vnet_used_elem_t ring[VNET_MAX_QUEUE];} vnet_used_t;
typedef struct {
 dma_page_t ring_pages[VNET_MAX_RING_PAGES];uint32_t ring_page_count;dma_page_t buffers[VNET_MAX_QUEUE];
 uint16_t size,avail_index,last_used;uint8_t device_owned[VNET_MAX_QUEUE],failed;vnet_desc_t *descriptors;vnet_avail_t *available;vnet_used_t *used;
} vnet_queue_t;
typedef struct {
 const struct pci_device *pci;uint16_t io_base;uint32_t host_features;uint8_t has_status,ready,registered,selftest_handler_registered,tx_failed;
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
static inline void vnet_dma_barrier(void){__asm__ volatile("mfence":::"memory");}
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

/*
 * The legacy transport acknowledges reset by clearing device status. DMA
 * memory must remain allocated if that acknowledgement never arrives.
 */
static int vnet_reset_device(void)
{
 if(!state.io_base)return 0;
 out8(state.io_base+0x12U,0U);
 for(uint32_t i=0U;i<VNET_WAIT_LIMIT;i++){
  __asm__ volatile("":::"memory");
  if(in8(state.io_base+0x12U)==0U)return 1;
  __asm__ volatile("pause");
 }
 return in8(state.io_base+0x12U)==0U;
}
#ifdef VANTAOS_SELF_TEST
static void vnet_rx_selftest_handler(net_device_t*d,const uint8_t*f,size_t n,
                                     uint16_t type,size_t header,void*context){
 (void)context;
 if(d!=&state.netdev||!f||type!=0x88B5U||header>n||n-header<4U)return;
 if(f[header]==0x56U&&f[header+1U]==0x41U&&
    f[header+2U]==0x4EU&&f[header+3U]==0x54)
  serial_write("VANTAOS_VIRTIO_NET_RX_SELFTEST_OK\n");
}
#endif
static int vnet_transmit(net_device_t*d,const uint8_t*f,size_t n){
 if(d!=&state.netdev||!state.ready||!state.tx.size||!f||
    n<NET_ETHERNET_HEADER_SIZE||n>NET_FRAME_MAX_SIZE||
    n+VNET_NET_HDR_SIZE>VNET_BUFFER_SIZE)return 0;
 spinlock_lock(&state.tx_lock);
 if(!state.ready||state.tx_failed){spinlock_unlock(&state.tx_lock);return 0;}
 vnet_queue_t*q=&state.tx;
 uint16_t slot=(uint16_t)(q->avail_index%q->size);
 uint16_t target=(uint16_t)(q->avail_index+1U);
 uint8_t*b=(uint8_t*)q->buffers[slot].virtual_address;
 for(uint32_t i=0;i<VNET_NET_HDR_SIZE;i++)b[i]=0;
 for(size_t i=0;i<n;i++)b[VNET_NET_HDR_SIZE+i]=f[i];
 q->descriptors[slot].address=q->buffers[slot].physical_address;
 q->descriptors[slot].length=(uint32_t)(VNET_NET_HDR_SIZE+n);
 q->descriptors[slot].flags=0;q->descriptors[slot].next=0;
 q->available->ring[q->avail_index%q->size]=slot;
 q->avail_index=target;vnet_dma_barrier();q->available->index=target;
 vnet_dma_barrier();
 queue_notify(state.io_base,1U);
 int done=0;
 for(uint32_t w=0;w<VNET_WAIT_LIMIT;w++){
  __asm__ volatile("":::"memory");
  uint16_t pending=(uint16_t)(q->used->index-q->last_used);
  if(pending){
   vnet_dma_barrier();
   if(pending==1U&&q->used->ring[q->last_used%q->size].id==slot){
    ++q->last_used;done=1;
   }
   break;
  }
  __asm__ volatile("pause");
 }
 if(!done)state.tx_failed=1U;
 spinlock_unlock(&state.tx_lock);
 return done;
}
static int vnet_poll_device(void){
 if(!state.ready)return 0;
 vnet_queue_t*q=&state.rx;int count=0;
 if(q->failed||!q->size)return 0;
 for(uint32_t budget=0;budget<64U;budget++){
  __asm__ volatile("":::"memory");
  uint16_t pending=(uint16_t)(q->used->index-q->last_used);
  if(!pending)break;
  if(pending>q->size){q->failed=1U;break;}
  vnet_dma_barrier();
  vnet_used_elem_t e=q->used->ring[q->last_used%q->size];
  ++q->last_used;
  if(e.id>=q->size||!q->device_owned[e.id]){q->failed=1U;break;}
  uint16_t id=(uint16_t)e.id;
  q->device_owned[id]=0U;
  uint8_t*b=(uint8_t*)q->buffers[id].virtual_address;
  if(e.length>=VNET_NET_HDR_SIZE+NET_ETHERNET_HEADER_SIZE&&e.length<=VNET_BUFFER_SIZE){
   size_t n=e.length-VNET_NET_HDR_SIZE;
   if(n<=NET_FRAME_MAX_SIZE)(void)net_receive_frame(&state.netdev,b+VNET_NET_HDR_SIZE,n);
  }
  q->descriptors[id].address=q->buffers[id].physical_address;
  q->descriptors[id].length=VNET_BUFFER_SIZE;
  q->descriptors[id].flags=VNET_DESC_WRITE;q->descriptors[id].next=0;
  q->available->ring[q->avail_index%q->size]=id;
  ++q->avail_index;q->device_owned[id]=1U;
  vnet_dma_barrier();q->available->index=q->avail_index;
  vnet_dma_barrier();queue_notify(state.io_base,0U);++count;
 }
 if(state.has_status){
  int up=(in16(state.io_base+0x1AU)&1U)!=0U;
  if((int)state.netdev.link_up!=up)(void)net_device_set_link(&state.netdev,up);
 }
 return count;
}
static void vnet_release(void){
 int reset_complete=state.io_base?vnet_reset_device():1;
 state.ready=0;
#ifdef VANTAOS_SELF_TEST
 if(state.selftest_handler_registered){
  (void)net_ethernet_handler_unregister(0x88B5U,vnet_rx_selftest_handler,0);
  state.selftest_handler_registered=0;
 }
#endif
 if(state.registered){(void)net_device_unregister(&state.netdev);state.registered=0;}
 if(reset_complete){
  queue_release(&state.rx);queue_release(&state.tx);
  state.io_base=0;state.pci=0;
 }else{
  /*
   * The device may still own descriptors. Retain queue metadata and the
   * transport identity as a quarantine: a later probe must not overwrite
   * these pointers or configure a second queue set while DMA may continue.
   */
  serial_write("VANTAOS_VIRTIO_NET_RESET_TIMEOUT_DMA_RETAINED\\n");
 }
}
static net_pci_probe_result_t vnet_probe(const struct pci_device*pci){
 if(!pci||state.ready||state.pci)return NET_PCI_PROBE_NOT_MINE;
 uint64_t bar=0;int io=0;if(!pci_get_bar(pci,0,&bar,&io,0)||!io||!bar||bar>0xFFFFU||bar+0x1BU>0xFFFFU)return NET_PCI_PROBE_NOT_MINE;
 if(!pci_enable_io_space(pci))return NET_PCI_PROBE_FAILED;
 state.pci=pci;state.io_base=(uint16_t)bar;
 if(!vnet_reset_device())goto fail;
 out8(state.io_base+0x12U,VNET_STATUS_ACK);out8(state.io_base+0x12U,VNET_STATUS_ACK|VNET_STATUS_DRIVER);
 state.host_features=in32(state.io_base);if(!(state.host_features&VNET_F_MAC))goto fail;
 state.has_status=(state.host_features&VNET_F_STATUS)!=0U;out32(state.io_base+0x04U,VNET_F_MAC|(state.has_status?VNET_F_STATUS:0U));
 out16(state.io_base+0x0EU,0U);uint16_t rn=in16(state.io_base+0x0CU);out16(state.io_base+0x0EU,1U);uint16_t tn=in16(state.io_base+0x0CU);
 if(!queue_allocate(&state.rx,rn)||!queue_allocate(&state.tx,tn))goto fail;
 for(uint16_t i=0;i<state.rx.size;i++){state.rx.descriptors[i].address=state.rx.buffers[i].physical_address;state.rx.descriptors[i].length=VNET_BUFFER_SIZE;state.rx.descriptors[i].flags=VNET_DESC_WRITE;state.rx.descriptors[i].next=0;state.rx.available->ring[i]=i;state.rx.device_owned[i]=1U;}
 state.rx.avail_index=state.rx.size;vnet_dma_barrier();state.rx.available->index=state.rx.avail_index;vnet_dma_barrier();queue_publish(state.io_base,0U,&state.rx);queue_publish(state.io_base,1U,&state.tx);
 uint8_t mac[6],any=0;for(uint32_t i=0;i<6U;i++){mac[i]=in8(state.io_base+0x14U+(uint16_t)i);any|=mac[i];}if(!any||(mac[0]&1U))goto fail;
 if(!pci_enable_bus_mastering(pci))goto fail;
 out8(state.io_base+0x12U,VNET_STATUS_ACK|VNET_STATUS_DRIVER|VNET_STATUS_DRIVER_OK);
 uint8_t *ndzero=(uint8_t*)&state.netdev;for(size_t i=0;i<sizeof(state.netdev);i++)ndzero[i]=0;
 state.netdev.name[0]='v';state.netdev.name[1]='n';state.netdev.name[2]='e';state.netdev.name[3]='t';state.netdev.name[4]='0';
 for(uint32_t i=0;i<6U;i++)state.netdev.mac[i]=mac[i];
 state.netdev.mtu=NET_MTU_DEFAULT;
 state.netdev.link_up=state.has_status?((in16(state.io_base+0x1AU)&1U)!=0U):1U;
 state.netdev.ops=&vnet_net_ops;state.netdev.driver_data=&state;
 state.netdev.parent_device=(device_t*)&state.pci->core;
 spinlock_init(&state.tx_lock);if(!net_device_register(&state.netdev))goto fail;state.registered=1;
#ifdef VANTAOS_SELF_TEST
 if(!net_device_configure_ipv4(&state.netdev,0x0A00020FU,0xFFFFFF00U,0x0A000202U)||
    !net_ethernet_handler_register(0x88B5U,vnet_rx_selftest_handler,0))goto fail;
 state.selftest_handler_registered=1U;
#endif
 state.ready=1;queue_notify(state.io_base,0U);
#ifdef VANTAOS_SELF_TEST
 uint8_t probe[18]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],0x88,0xB5,0x56,0x41,0x4E,0x54};
 if(net_device_transmit(&state.netdev,probe,sizeof(probe)))serial_write("VANTAOS_VIRTIO_NET_TX_SELFTEST_OK\n");
 else serial_write("VANTAOS_VIRTIO_NET_TX_SELFTEST_FAIL\n");
#endif
 serial_write("VANTAOS_VIRTIO_NET_INITIALIZED\n");return NET_PCI_PROBE_BOUND;
fail: vnet_release();return NET_PCI_PROBE_FAILED;
}
int virtio_net_initialize(void){uint8_t*p=(uint8_t*)&state;for(size_t i=0;i<sizeof(state);i++)p[i]=0;spinlock_init(&state.tx_lock);return net_pci_driver_register(&vnet_driver);}
