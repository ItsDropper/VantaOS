#include "e1000.h"
#include "net.h"
#include "dma.h"
#include "mmio.h"
#include "paging.h"
#include "pci.h"
#include "serial.h"
#include "spinlock.h"
#include <stddef.h>
#include <stdint.h>

#define E1000_VENDOR_ID 0x8086U
#define E1000_DEVICE_82540EM 0x100EU
#define E1000_MMIO_SIZE 0x20000U
#define E1000_RING_COUNT 32U
#define E1000_TX_COUNT 8U
#define E1000_BUFFER_SIZE 2048U
#define E1000_WAIT_LIMIT 10000000U
#define REG_CTRL 0x0000U
#define REG_STATUS 0x0008U
#define REG_EERD 0x0014U
#define REG_ICR 0x00C0U
#define REG_IMC 0x00D8U
#define REG_RCTL 0x0100U
#define REG_TCTL 0x0400U
#define REG_TIPG 0x0410U
#define REG_RAL 0x5400U
#define REG_RAH 0x5404U
#define REG_RDBAL 0x2800U
#define REG_RDBAH 0x2804U
#define REG_RDLEN 0x2808U
#define REG_RDH 0x2810U
#define REG_RDT 0x2818U
#define REG_TDBAL 0x3800U
#define REG_TDBAH 0x3804U
#define REG_TDLEN 0x3808U
#define REG_TDH 0x3810U
#define REG_TDT 0x3818U
#define CTRL_RST (1U << 26)
#define STATUS_LU (1U << 1)
#define RCTL_EN (1U << 1)
#define RCTL_BAM (1U << 15)
#define RCTL_SECRC (1U << 26)
#define TCTL_EN (1U << 1)
#define TCTL_PSP (1U << 3)
#define RX_DD 0x01U
#define RX_EOP 0x02U
#define TX_DD 0x01U
#define TX_CMD_EOP 0x01U
#define TX_CMD_IFCS 0x02U
#define TX_CMD_RS 0x08U

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} e1000_rx_desc_t;
typedef struct __attribute__((packed)) {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} e1000_tx_desc_t;
_Static_assert(sizeof(e1000_rx_desc_t)==16U,"E1000 RX descriptor size");
_Static_assert(sizeof(e1000_tx_desc_t)==16U,"E1000 TX descriptor size");

typedef struct {
    const struct pci_device* pci;
    volatile uint8_t* registers;
    void* mmio;
    dma_page_t rx_ring_page,tx_ring_page;
    dma_page_t rx_buffers[E1000_RING_COUNT];
    dma_page_t tx_buffers[E1000_TX_COUNT];
    net_device_t netdev;
    spinlock_t tx_lock;
    uint32_t rx_next,tx_next;
    uint8_t ready;
} e1000_state_t;

static e1000_state_t state;
static int e1000_poll_device(void);
static int e1000_probe(const struct pci_device* pci);

static const net_pci_id_t e1000_ids[] = {
    {E1000_VENDOR_ID,E1000_DEVICE_82540EM,0x02U,0x00U,0x00U}
};
static int e1000_transmit(net_device_t*,const uint8_t*,size_t);
static const net_device_ops_t e1000_net_ops = { .transmit = e1000_transmit };
static const net_pci_driver_t e1000_pci_driver = {
    .name="e1000",
    .ids=e1000_ids,
    .id_count=sizeof(e1000_ids)/sizeof(e1000_ids[0]),
    .probe=e1000_probe,
    .poll=e1000_poll_device
};

static uint32_t reg_read(uint32_t offset)
{
    return *(volatile uint32_t*)(state.registers+offset);
}
static void reg_write(uint32_t offset,uint32_t value)
{
    *(volatile uint32_t*)(state.registers+offset)=value;
    (void)*(volatile uint32_t*)(state.registers+REG_STATUS);
    __asm__ volatile ("" ::: "memory");
}
static int wait_register(uint32_t offset,uint32_t mask,uint32_t expected)
{
    for(uint32_t i=0;i<E1000_WAIT_LIMIT;i++){
        if((reg_read(offset)&mask)==expected)return 1;
        __asm__ volatile("pause");
    }
    return 0;
}
static int valid_mac(const uint8_t* mac)
{
    uint8_t any=0;
    for(uint32_t i=0;i<6;i++)any|=mac[i];
    return any!=0U&&!(mac[0]&1U);
}
static int read_eeprom_word(uint8_t index,uint16_t* value)
{
    reg_write(REG_EERD,1U|((uint32_t)index<<8));
    for(uint32_t i=0;i<E1000_WAIT_LIMIT;i++){
        uint32_t result=reg_read(REG_EERD);
        if(result&(1U<<4)){*value=(uint16_t)(result>>16);return 1;}
        __asm__ volatile("pause");
    }
    return 0;
}
static int read_mac(uint8_t mac[6])
{
    uint32_t low=reg_read(REG_RAL),high=reg_read(REG_RAH);
    if(high&(1U<<31)){
        mac[0]=(uint8_t)low;mac[1]=(uint8_t)(low>>8);
        mac[2]=(uint8_t)(low>>16);mac[3]=(uint8_t)(low>>24);
        mac[4]=(uint8_t)high;mac[5]=(uint8_t)(high>>8);
    }else{
        for(uint8_t i=0;i<3;i++){
            uint16_t word;
            if(!read_eeprom_word(i,&word))return 0;
            mac[i*2U]=(uint8_t)word;mac[i*2U+1U]=(uint8_t)(word>>8);
        }
    }
    return valid_mac(mac);
}
static void release_dma(void)
{
    for(uint32_t i=0;i<E1000_RING_COUNT;i++)dma_free_page(&state.rx_buffers[i]);
    for(uint32_t i=0;i<E1000_TX_COUNT;i++)dma_free_page(&state.tx_buffers[i]);
    dma_free_page(&state.rx_ring_page);
    dma_free_page(&state.tx_ring_page);
}
static int allocate_dma(void)
{
    if(!dma_alloc_page(&state.rx_ring_page)||!dma_alloc_page(&state.tx_ring_page))return 0;
    for(uint32_t i=0;i<E1000_RING_COUNT;i++)if(!dma_alloc_page(&state.rx_buffers[i]))return 0;
    for(uint32_t i=0;i<E1000_TX_COUNT;i++)if(!dma_alloc_page(&state.tx_buffers[i]))return 0;
    return 1;
}
static int e1000_transmit(net_device_t* device,const uint8_t* frame,size_t length)
{
    if(device!=&state.netdev||!state.ready||!frame||
       length<NET_ETHERNET_HEADER_SIZE||length>NET_FRAME_MAX_SIZE)return 0;
    spinlock_lock(&state.tx_lock);
    volatile e1000_tx_desc_t* ring=(volatile e1000_tx_desc_t*)state.tx_ring_page.virtual_address;
    uint32_t index=state.tx_next;
    volatile e1000_tx_desc_t* desc=&ring[index];
    if(!(desc->status&TX_DD)){
        spinlock_unlock(&state.tx_lock);return 0;
    }
    uint8_t* buffer=(uint8_t*)state.tx_buffers[index].virtual_address;
    for(size_t i=0;i<length;i++)buffer[i]=frame[i];
    desc->address=state.tx_buffers[index].physical_address;
    desc->length=(uint16_t)length;
    desc->checksum_offset=0;desc->checksum_start=0;desc->special=0;
    desc->status=0;
    desc->command=TX_CMD_EOP|TX_CMD_IFCS|TX_CMD_RS;
    __asm__ volatile("" ::: "memory");
    state.tx_next=(index+1U)%E1000_TX_COUNT;
    reg_write(REG_TDT,state.tx_next);
    int complete=0;
    for(uint32_t spin=0;spin<E1000_WAIT_LIMIT;spin++){
        if(desc->status&TX_DD){complete=1;break;}
        __asm__ volatile("pause");
    }
    spinlock_unlock(&state.tx_lock);
    return complete;
}
static int e1000_poll_device(void)
{
    if(!state.ready)return 0;
    (void)net_device_set_link(&state.netdev,(reg_read(REG_STATUS)&STATUS_LU)!=0U);
    volatile e1000_rx_desc_t* ring=(volatile e1000_rx_desc_t*)state.rx_ring_page.virtual_address;
    int received=0;
    for(uint32_t count=0;count<E1000_RING_COUNT;count++){
        uint32_t index=state.rx_next;
        volatile e1000_rx_desc_t* desc=&ring[index];
        __asm__ volatile("" ::: "memory");
        if(!(desc->status&RX_DD))break;
        uint16_t length=desc->length;
        if((desc->status&RX_EOP)&&desc->errors==0U&&
           length>=NET_ETHERNET_HEADER_SIZE&&length<=NET_FRAME_MAX_SIZE)
            (void)net_receive_frame(&state.netdev,state.rx_buffers[index].virtual_address,length);
        desc->length=0;desc->checksum=0;desc->errors=0;desc->special=0;desc->status=0;
        __asm__ volatile("" ::: "memory");
        state.rx_next=(index+1U)%E1000_RING_COUNT;
        reg_write(REG_RDT,index);
        ++received;
    }
    return received;
}
static int e1000_probe(const struct pci_device* pci)
{
    if(!pci||state.ready)return NET_PCI_PROBE_FAILED;
    uint64_t bar=0;int is_io=0,is_64=0;
    if(!pci_get_bar(pci,0,&bar,&is_io,&is_64)||is_io||!bar)return NET_PCI_PROBE_FAILED;
    if(!pci_enable_memory_space(pci))return NET_PCI_PROBE_FAILED;
    state.pci=pci;
    state.mmio=mmio_map_region(bar,E1000_MMIO_SIZE);
    if(!state.mmio){state.pci=0;return NET_PCI_PROBE_FAILED;}
    state.registers=(volatile uint8_t*)state.mmio;
    reg_write(REG_IMC,0xFFFFFFFFU);
    (void)reg_read(REG_ICR);
    reg_write(REG_CTRL,reg_read(REG_CTRL)|CTRL_RST);
    if(!wait_register(REG_CTRL,CTRL_RST,0U))goto fail_mmio;
    if(!allocate_dma())goto fail_dma;
    uint8_t mac[6];
    if(!read_mac(mac))goto fail_dma;
    e1000_rx_desc_t* rx=(e1000_rx_desc_t*)state.rx_ring_page.virtual_address;
    e1000_tx_desc_t* tx=(e1000_tx_desc_t*)state.tx_ring_page.virtual_address;
    for(uint32_t i=0;i<E1000_RING_COUNT;i++){
        rx[i].address=state.rx_buffers[i].physical_address;
        rx[i].length=0;rx[i].checksum=0;rx[i].status=0;rx[i].errors=0;rx[i].special=0;
    }
    for(uint32_t i=0;i<E1000_TX_COUNT;i++){
        tx[i].address=state.tx_buffers[i].physical_address;
        tx[i].length=0;tx[i].checksum_offset=0;tx[i].checksum_start=0;
        tx[i].command=0;tx[i].status=TX_DD;tx[i].special=0;
    }
    reg_write(REG_RDBAL,(uint32_t)state.rx_ring_page.physical_address);
    reg_write(REG_RDBAH,(uint32_t)(state.rx_ring_page.physical_address>>32));
    reg_write(REG_RDLEN,E1000_RING_COUNT*sizeof(e1000_rx_desc_t));
    reg_write(REG_RDH,0);reg_write(REG_RDT,E1000_RING_COUNT-1U);
    reg_write(REG_TDBAL,(uint32_t)state.tx_ring_page.physical_address);
    reg_write(REG_TDBAH,(uint32_t)(state.tx_ring_page.physical_address>>32));
    reg_write(REG_TDLEN,E1000_TX_COUNT*sizeof(e1000_tx_desc_t));
    reg_write(REG_TDH,0);reg_write(REG_TDT,0);
    reg_write(REG_TIPG,0x0060200AU);
    state.rx_next=0;state.tx_next=0;
    state.netdev=(net_device_t){.name="en0",.mac={mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]},
        .mtu=NET_MTU_DEFAULT,.link_up=(reg_read(REG_STATUS)&STATUS_LU)?1U:0U,
        .ops=&e1000_net_ops,.driver_data=&state};
    /* Publish descriptors only after their DMA memory and PCI command state are valid. */
    if(!pci_enable_bus_mastering(pci))goto fail_dma;
    if(!net_device_register(&state.netdev))goto fail_dma;
    /* No engine is active until registration succeeds, so failure cleanup is DMA-safe. */
    state.ready=1;
    reg_write(REG_TCTL,TCTL_EN|TCTL_PSP|0x00040100U);
    reg_write(REG_RCTL,RCTL_EN|RCTL_BAM|RCTL_SECRC);
    serial_write("VANTAOS_E1000_INITIALIZED\n");
#ifdef VANTAOS_SELF_TEST
    (void)e1000_poll_device();
    {
        uint8_t probe[NET_ETHERNET_HEADER_SIZE+4]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],0x88,0xB5,0x56,0x41,0x4E,0x54};
        if(net_device_transmit(&state.netdev,probe,sizeof(probe)))
            serial_write("VANTAOS_E1000_TX_SELFTEST_OK\n");
        else
            serial_write("VANTAOS_E1000_TX_SELFTEST_FAIL\n");
    }
#endif
    return NET_PCI_PROBE_BOUND;
fail_dma:
    release_dma();
fail_mmio:
    if(state.mmio)(void)mmio_unmap_region(state.mmio,E1000_MMIO_SIZE);
    state.mmio=0;state.registers=0;state.pci=0;state.ready=0;
    return NET_PCI_PROBE_FAILED;
}
int e1000_initialize(void)
{
    state=(e1000_state_t){0};
    spinlock_init(&state.tx_lock);
    return net_pci_driver_register(&e1000_pci_driver);
}
