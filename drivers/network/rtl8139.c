#include "net.h"
#include "dma.h"
#include "pci.h"
#include "serial.h"
#include "spinlock.h"
#include <stddef.h>
#include <stdint.h>

#define RTL_VENDOR_ID 0x10ECU
#define RTL8139_DEVICE_ID 0x8139U
#define RTL8139_RX_RING_SIZE 8192U
#define RTL8139_RX_PAGE_COUNT 3U
#define RTL8139_TX_SLOTS 4U
#define RTL8139_TX_BUFFER_SIZE 2048U
#define RTL8139_IO_SIZE 0x100U
#define RTL8139_WAIT_LIMIT 1000000U
#define RTL8139_REG_MAC 0x00U
#define RTL8139_REG_TSD0 0x10U
#define RTL8139_REG_TSAD0 0x20U
#define RTL8139_REG_RBSTART 0x30U
#define RTL8139_REG_CR 0x37U
#define RTL8139_REG_CAPR 0x38U
#define RTL8139_REG_CBR 0x3AU
#define RTL8139_REG_IMR 0x3CU
#define RTL8139_REG_ISR 0x3EU
#define RTL8139_REG_TCR 0x40U
#define RTL8139_REG_RCR 0x44U
#define RTL8139_REG_CONFIG1 0x52U
#define RTL8139_REG_MSR 0x58U
#define RTL8139_CR_BUFE 0x01U
#define RTL8139_CR_TE 0x04U
#define RTL8139_CR_RE 0x08U
#define RTL8139_CR_RST 0x10U
#define RTL8139_RX_ROK 0x0001U
#define RTL8139_TX_TOK 0x00002000U
#define RTL8139_RX_ACCEPT_ALL 0x0000008FU /* physical, multicast, broadcast, wrap */

typedef struct {
    const struct pci_device *pci;
    uint16_t io_base;
    dma_page_t rx_pages[RTL8139_RX_PAGE_COUNT];
    dma_page_t tx_pages[RTL8139_TX_SLOTS];
    net_device_t netdev;
    spinlock_t tx_lock;
    uint16_t rx_offset;
    uint8_t ready;
    uint8_t registered;
} rtl8139_state_t;

static rtl8139_state_t state;
static int rtl8139_poll_device(void);
static net_pci_probe_result_t rtl8139_probe(const struct pci_device *pci);
static int rtl8139_transmit(net_device_t *device, const uint8_t *frame, size_t length);

static const net_pci_id_t rtl8139_ids[] = {
    {RTL_VENDOR_ID, RTL8139_DEVICE_ID, 0x02U, 0x00U, 0x00U}
};
static const net_device_ops_t rtl8139_net_ops = {.transmit = rtl8139_transmit};
static const net_pci_driver_t rtl8139_driver = {
    .name = "rtl8139",
    .ids = rtl8139_ids,
    .id_count = sizeof(rtl8139_ids) / sizeof(rtl8139_ids[0]),
    .probe = rtl8139_probe,
    .poll = rtl8139_poll_device
};

static inline void io_out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0,%1" :: "a"(value), "Nd"(port));
}
static inline void io_out16(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0,%1" :: "a"(value), "Nd"(port));
}
static inline void io_out32(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0,%1" :: "a"(value), "Nd"(port));
}
static inline uint8_t io_in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline uint16_t io_in16(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline uint32_t io_in32(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}

static void release_dma(void)
{
    for (uint32_t i = 0; i < RTL8139_TX_SLOTS; ++i)
        dma_free_page(&state.tx_pages[i]);
    dma_free_contiguous(state.rx_pages, RTL8139_RX_PAGE_COUNT);
}

static int allocate_dma(void)
{
    if (!dma_alloc_contiguous(state.rx_pages, RTL8139_RX_PAGE_COUNT))
        return 0;
    for (uint32_t i = 0; i < RTL8139_TX_SLOTS; ++i) {
        if (!dma_alloc_page(&state.tx_pages[i]))
            return 0;
    }
    return 1;
}

static int rtl8139_transmit(net_device_t *device, const uint8_t *frame, size_t length)
{
    if (device != &state.netdev || !state.ready || !frame ||
        length < NET_ETHERNET_HEADER_SIZE || length > NET_FRAME_MAX_SIZE ||
        length > RTL8139_TX_BUFFER_SIZE)
        return 0;

    uint64_t flags = spinlock_lock_irqsave(&state.tx_lock);
    uint32_t slot = (uint32_t)(state.netdev.driver_data ?
        (uintptr_t)state.netdev.driver_data : 0U);
    /* driver_data is the state pointer; tx slot ownership is kept separately. */
    (void)slot;
    static uint32_t tx_next;
    slot = tx_next % RTL8139_TX_SLOTS;
    uint16_t tsd = (uint16_t)(state.io_base + RTL8139_REG_TSD0 + slot * 4U);
    int completed = 0;
    for (uint32_t spin = 0; spin < RTL8139_WAIT_LIMIT; ++spin) {
        if (io_in32(tsd) & RTL8139_TX_TOK) {
            completed = 1;
            break;
        }
        __asm__ volatile ("pause");
    }
    if (!completed) {
        spinlock_unlock_irqrestore(&state.tx_lock, flags);
        return 0;
    }

    uint8_t *buffer = (uint8_t *)state.tx_pages[slot].virtual_address;
    for (size_t i = 0; i < length; ++i)
        buffer[i] = frame[i];
    __asm__ volatile ("" ::: "memory");
    io_out32((uint16_t)(state.io_base + RTL8139_REG_TSAD0 + slot * 4U),
             (uint32_t)state.tx_pages[slot].physical_address);
    io_out32(tsd, (uint32_t)length);
    tx_next = (slot + 1U) % RTL8139_TX_SLOTS;

    completed = 0;
    for (uint32_t spin = 0; spin < RTL8139_WAIT_LIMIT; ++spin) {
        __asm__ volatile ("" ::: "memory");
        if (io_in32(tsd) & RTL8139_TX_TOK) {
            completed = 1;
            break;
        }
        __asm__ volatile ("pause");
    }
    spinlock_unlock_irqrestore(&state.tx_lock, flags);
    return completed;
}

static uint8_t rx_read8(uint16_t offset)
{
    const volatile uint8_t *ring =
        (const volatile uint8_t *)state.rx_pages[0].virtual_address;
    return ring[offset % RTL8139_RX_RING_SIZE];
}
static uint16_t rx_read16(uint16_t offset)
{
    return (uint16_t)(rx_read8(offset) | ((uint16_t)rx_read8((uint16_t)(offset + 1U)) << 8));
}
static void rx_copy(uint16_t offset, uint8_t *out, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        out[i] = rx_read8((uint16_t)(offset + i));
}

static int rtl8139_poll_device(void)
{
    if (!state.ready)
        return 0;

    int received = 0;
    uint16_t producer = io_in16((uint16_t)(state.io_base + RTL8139_REG_CBR));
    for (uint32_t budget = 0; budget < 32U; ++budget) {
        uint8_t command = io_in8((uint16_t)(state.io_base + RTL8139_REG_CR));
        if ((command & RTL8139_CR_BUFE) || state.rx_offset == producer)
            break;

        uint16_t status = rx_read16(state.rx_offset);
        uint16_t packet_length = rx_read16((uint16_t)(state.rx_offset + 2U));
        if (packet_length < 18U || packet_length > NET_FRAME_MAX_SIZE + 4U) {
            /* A corrupt ring header is recovered by resetting the receive pointer. */
            state.rx_offset = producer;
            io_out16((uint16_t)(state.io_base + RTL8139_REG_CAPR),
                     (uint16_t)(state.rx_offset - 16U));
            break;
        }

        size_t frame_length = (size_t)packet_length - 4U; /* hardware includes the FCS */
        if ((status & RTL8139_RX_ROK) && frame_length >= NET_ETHERNET_HEADER_SIZE &&
            frame_length <= NET_FRAME_MAX_SIZE) {
            uint8_t frame[NET_FRAME_MAX_SIZE];
            rx_copy((uint16_t)(state.rx_offset + 4U), frame, frame_length);
            (void)net_receive_frame(&state.netdev, frame, frame_length);
            ++received;
        }

        uint32_t next = ((uint32_t)state.rx_offset + 4U + packet_length + 3U) & ~3U;
        state.rx_offset = (uint16_t)(next % RTL8139_RX_RING_SIZE);
        io_out16((uint16_t)(state.io_base + RTL8139_REG_CAPR),
                 (uint16_t)(state.rx_offset - 16U));
        producer = io_in16((uint16_t)(state.io_base + RTL8139_REG_CBR));
    }

    /* Clear latched status bits and update the link state from the media register. */
    io_out16((uint16_t)(state.io_base + RTL8139_REG_ISR), 0xFFFFU);
    int link_up = (io_in8((uint16_t)(state.io_base + RTL8139_REG_MSR)) & 0x04U) == 0U;
    if (state.netdev.link_up != (uint8_t)link_up)
        (void)net_device_set_link(&state.netdev, link_up);
    return received;
}

static net_pci_probe_result_t rtl8139_probe(const struct pci_device *pci)
{
    if (!pci || state.ready || state.registered)
        return NET_PCI_PROBE_NOT_MINE;

    uint64_t bar = 0;
    int is_io = 0;
    if (!pci_get_bar(pci, 0U, &bar, &is_io, 0) || !is_io ||
        !bar || bar > 0xFFFFU || bar + RTL8139_IO_SIZE - 1U > 0xFFFFU)
        return NET_PCI_PROBE_NOT_MINE;
    if (!pci_enable_io_space(pci))
        return NET_PCI_PROBE_FAILED;

    state.pci = pci;
    state.io_base = (uint16_t)bar;
    io_out8((uint16_t)(state.io_base + RTL8139_REG_CONFIG1), 0U);
    io_out8((uint16_t)(state.io_base + RTL8139_REG_CR), RTL8139_CR_RST);
    uint32_t spin = 0;
    while ((io_in8((uint16_t)(state.io_base + RTL8139_REG_CR)) & RTL8139_CR_RST) &&
           spin++ < RTL8139_WAIT_LIMIT)
        __asm__ volatile ("pause");
    if (spin >= RTL8139_WAIT_LIMIT)
        goto fail;

    uint8_t mac[6];
    uint8_t any = 0U;
    for (uint32_t i = 0; i < 6U; ++i) {
        mac[i] = io_in8((uint16_t)(state.io_base + RTL8139_REG_MAC + i));
        any |= mac[i];
    }
    if (!any || (mac[0] & 1U) || !allocate_dma())
        goto fail;

    uint8_t *rx = (uint8_t *)state.rx_pages[0].virtual_address;
    for (size_t i = 0; i < RTL8139_RX_RING_SIZE + 16U; ++i)
        rx[i] = 0U;
    for (uint32_t i = 0; i < RTL8139_TX_SLOTS; ++i) {
        uint8_t *tx = (uint8_t *)state.tx_pages[i].virtual_address;
        for (size_t j = 0; j < RTL8139_TX_BUFFER_SIZE; ++j)
            tx[j] = 0U;
    }

    io_out32((uint16_t)(state.io_base + RTL8139_REG_RBSTART),
             (uint32_t)state.rx_pages[0].physical_address);
    io_out16((uint16_t)(state.io_base + RTL8139_REG_CAPR), 0xFFF0U);
    io_out16((uint16_t)(state.io_base + RTL8139_REG_IMR), 0U); /* bounded polling, no IRQ dependency */
    io_out16((uint16_t)(state.io_base + RTL8139_REG_ISR), 0xFFFFU);
    io_out32((uint16_t)(state.io_base + RTL8139_REG_TCR), 0x03000700U);
    io_out32((uint16_t)(state.io_base + RTL8139_REG_RCR), RTL8139_RX_ACCEPT_ALL);

    state.rx_offset = 0U;
    state.netdev = (net_device_t){
        .name = "rt0",
        .mac = {mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]},
        .mtu = NET_MTU_DEFAULT,
        .link_up = 1U,
        .ops = &rtl8139_net_ops,
        .driver_data = &state
    };
    spinlock_init(&state.tx_lock);
    if (!pci_enable_bus_mastering(pci))
        goto fail;
    if (!net_device_register(&state.netdev))
        goto fail;
    state.registered = 1U;
    state.ready = 1U;
    io_out8((uint16_t)(state.io_base + RTL8139_REG_CR),
            RTL8139_CR_RE | RTL8139_CR_TE);
    (void)rtl8139_poll_device();
    serial_write("VANTAOS_RTL8139_INITIALIZED\n");
#ifdef VANTAOS_SELF_TEST
    {
        const uint8_t probe[18] = {
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],
            0x88,0xB5,0x56,0x41,0x4E,0x54
        };
        if (net_device_transmit(&state.netdev, probe, sizeof(probe)))
            serial_write("VANTAOS_RTL8139_TX_SELFTEST_OK\n");
        else
            serial_write("VANTAOS_RTL8139_TX_SELFTEST_FAIL\n");
    }
#endif
    return NET_PCI_PROBE_BOUND;

fail:
    if (state.io_base) {
        io_out8((uint16_t)(state.io_base + RTL8139_REG_CR), 0U);
        io_out16((uint16_t)(state.io_base + RTL8139_REG_IMR), 0U);
    }
    if (state.rx_pages[0].virtual_address || state.rx_pages[1].virtual_address ||
        state.rx_pages[2].virtual_address || state.tx_pages[0].virtual_address ||
        state.tx_pages[1].virtual_address || state.tx_pages[2].virtual_address ||
        state.tx_pages[3].virtual_address)
        release_dma();
    if (state.registered)
        (void)net_device_unregister(&state.netdev);
    state = (rtl8139_state_t){0};
    return NET_PCI_PROBE_FAILED;
}

int rtl8139_initialize(void)
{
    state = (rtl8139_state_t){0};
    spinlock_init(&state.tx_lock);
    return net_pci_driver_register(&rtl8139_driver);
}
