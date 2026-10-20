/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "net.h"
#include "dma.h"
#include "mmio.h"
#include "pci.h"
#include "serial.h"
#include "spinlock.h"
#include <stddef.h>
#include <stdint.h>

#define VNET_VENDOR 0x1AF4U
#define VNET_DEVICE 0x1041U
#define VNET_CAP_ID 0x09U
#define VNET_CAP_COMMON 1U
#define VNET_CAP_NOTIFY 2U
#define VNET_CAP_DEVICE 4U
#define VNET_F_MAC (1ULL << 5)
#define VNET_F_STATUS (1ULL << 16)
#define VNET_F_MRG_RXBUF (1ULL << 15)
#define VNET_F_VERSION_1 (1ULL << 32)
#define VNET_STATUS_ACK 1U
#define VNET_STATUS_DRIVER 2U
#define VNET_STATUS_DRIVER_OK 4U
#define VNET_STATUS_FEATURES_OK 8U
#define VNET_QUEUE_LIMIT 128U
#define VNET_BUFFER_SIZE 2048U
#define VNET_HEADER_BASE_SIZE 10U
#define VNET_HEADER_MRG_SIZE 12U
#define VNET_WAIT_LIMIT 10000000U
#define VNET_PAGE_SIZE 4096U
#define VNET_DESC_WRITE 2U

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} vnet_desc_t;
typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[VNET_QUEUE_LIMIT];
} vnet_avail_t;
typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t length;
} vnet_used_elem_t;
typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t index;
    vnet_used_elem_t ring[VNET_QUEUE_LIMIT];
} vnet_used_t;

typedef struct {
    dma_page_t ring;
    dma_page_t buffers[VNET_QUEUE_LIMIT];
    uint16_t size;
    uint16_t avail_index;
    uint16_t last_used;
    uint16_t buffer_count;
    uint8_t ring_allocated;
    vnet_desc_t *desc;
    vnet_avail_t *avail;
    vnet_used_t *used;
    volatile uint8_t *notify;
} vnet_queue_t;

typedef struct {
    const struct pci_device *pci;
    volatile uint8_t *common;
    volatile uint8_t *notify_region;
    volatile uint8_t *device_config;
    uint32_t common_length;
    uint32_t notify_length;
    uint32_t notify_multiplier;
    uint32_t device_config_length;
    uint8_t has_status;
    uint8_t mergeable_rx;
    uint8_t ready;
    uint8_t registered;
    uint8_t selftest_handler_registered;
    net_device_t netdev;
    vnet_queue_t rx;
    vnet_queue_t tx;
    spinlock_t tx_lock;
} vnet_modern_state_t;

static vnet_modern_state_t state;
static int vnet_modern_poll(void);
static net_pci_probe_result_t vnet_modern_probe(const struct pci_device *);
static int vnet_modern_transmit(net_device_t *, const uint8_t *, size_t);
static const net_device_ops_t vnet_modern_ops = { .transmit = vnet_modern_transmit };
static const net_pci_id_t vnet_modern_ids[] = {
    { VNET_VENDOR, VNET_DEVICE, 0x02U, 0x00U, 0x00U }
};
static const net_pci_driver_t vnet_modern_driver = {
    .name = "virtio-net-modern",
    .ids = vnet_modern_ids,
    .id_count = sizeof(vnet_modern_ids) / sizeof(vnet_modern_ids[0]),
    .probe = vnet_modern_probe,
    .poll = vnet_modern_poll
};

static uint8_t rd8(volatile uint8_t *p, uint32_t o) { return p[o]; }
static uint16_t rd16(volatile uint8_t *p, uint32_t o) { return *(volatile uint16_t *)(p + o); }
static uint32_t rd32(volatile uint8_t *p, uint32_t o) { return *(volatile uint32_t *)(p + o); }
static void wr8(volatile uint8_t *p, uint32_t o, uint8_t v) { p[o] = v; }
static void wr16(volatile uint8_t *p, uint32_t o, uint16_t v) { *(volatile uint16_t *)(p + o) = v; }
static void wr32(volatile uint8_t *p, uint32_t o, uint32_t v) { *(volatile uint32_t *)(p + o) = v; }
static void wr64(volatile uint8_t *p, uint32_t o, uint64_t v)
{
    wr32(p, o, (uint32_t)v);
    wr32(p, o + 4U, (uint32_t)(v >> 32));
}
static void dma_barrier(void) { __asm__ volatile ("mfence" ::: "memory"); }
static size_t virtio_header_size(void)
{
    return state.mergeable_rx ? VNET_HEADER_MRG_SIZE : VNET_HEADER_BASE_SIZE;
}

static volatile uint8_t *map_cap(const struct pci_device *pci, uint8_t bar,
                                 uint32_t offset, uint32_t length)
{
    uint64_t base = 0;
    int io = 0;
    if (bar >= 6U || !length || offset >= 0x100000U ||
        length > 0x100000U - offset) {
        serial_write("VANTAOS_VIRTIO_NET_MODERN_MAP_BAD_RANGE\n");
        return 0;
    }
    if (!pci_get_bar(pci, bar, &base, &io, 0)) {
        uint32_t raw = pci_read_config32(pci, (uint8_t)(0x10U + (uint32_t)bar * 4U));
        if (raw == 0U) serial_write("VANTAOS_VIRTIO_NET_MODERN_MAP_BAR_ZERO\n");
        else if (raw == 0xFFFFFFFFU) serial_write("VANTAOS_VIRTIO_NET_MODERN_MAP_BAR_ABSENT\n");
        else if (!(raw & 1U) && ((raw >> 1U) & 3U) == 3U)
            serial_write("VANTAOS_VIRTIO_NET_MODERN_MAP_BAR_RESERVED_TYPE\n");
        else serial_write("VANTAOS_VIRTIO_NET_MODERN_MAP_BAR_INVALID_BASE\n");
        switch (bar) {
            case 0U: serial_write("VANTAOS_VIRTIO_NET_MODERN_MAP_BAR_INDEX_0\n"); break;
            case 1U: serial_write("VANTAOS_VIRTIO_NET_MODERN_MAP_BAR_INDEX_1\n"); break;
            case 2U: serial_write("VANTAOS_VIRTIO_NET_MODERN_MAP_BAR_INDEX_2\n"); break;
            case 3U: serial_write("VANTAOS_VIRTIO_NET_MODERN_MAP_BAR_INDEX_3\n"); break;
            case 4U: serial_write("VANTAOS_VIRTIO_NET_MODERN_MAP_BAR_INDEX_4\n"); break;
            default: serial_write("VANTAOS_VIRTIO_NET_MODERN_MAP_BAR_INDEX_5\n"); break;
        }
        return 0;
    }
    if (io || !base) {
        serial_write("VANTAOS_VIRTIO_NET_MODERN_MAP_BAR_NOT_MMIO\n");
        return 0;
    }
    if (base > UINT64_MAX - offset) {
        serial_write("VANTAOS_VIRTIO_NET_MODERN_MAP_BAR_OVERFLOW\n");
        return 0;
    }
    volatile uint8_t *mapped =
        (volatile uint8_t *)mmio_map_region(base + offset, length);
    if (!mapped) {
        serial_write("VANTAOS_VIRTIO_NET_MODERN_MAP_MMIO_FAIL\n");
        return 0;
    }
    return mapped;
}

static int discover_capabilities(const struct pci_device *pci)
{
    if (!(pci_read_config16(pci, 0x06U) & 0x10U)) {
        serial_write("VANTAOS_VIRTIO_NET_MODERN_CAP_NO_LIST\n");
        return 0;
    }
    uint8_t at = pci_read_config8(pci, 0x34U);
    uint8_t seen[256] = {0};
    while (at) {
        if (at < 0x40U || (at & 3U) || seen[at]) {
            serial_write("VANTAOS_VIRTIO_NET_MODERN_CAP_BAD_LINK\n");
            return 0;
        }
        seen[at] = 1U;
        uint8_t id = pci_read_config8(pci, at);
        uint8_t next = pci_read_config8(pci, (uint8_t)(at + 1U));
        if (id == VNET_CAP_ID) {
            uint8_t length = pci_read_config8(pci, (uint8_t)(at + 2U));
            if (length < 16U || (uint16_t)at + length > 256U) {
                serial_write("VANTAOS_VIRTIO_NET_MODERN_CAP_BAD_LENGTH\n");
                return 0;
            }
            uint8_t type = pci_read_config8(pci, (uint8_t)(at + 3U));
            /* PCI_CFG and ISR capabilities do not describe MMIO windows. */
            if (type != VNET_CAP_COMMON && type != VNET_CAP_NOTIFY &&
                type != VNET_CAP_DEVICE) {
                at = next;
                continue;
            }
            uint8_t bar = pci_read_config8(pci, (uint8_t)(at + 4U));
            uint32_t offset = pci_read_config32(pci, (uint8_t)(at + 8U));
            uint32_t region_length = pci_read_config32(pci, (uint8_t)(at + 12U));
            if (!region_length || offset >= 0x100000U ||
                region_length > 0x100000U - offset) {
                serial_write("VANTAOS_VIRTIO_NET_MODERN_CAP_BAD_REGION\n");
                return 0;
            }
            if (type == VNET_CAP_COMMON) {
                if (state.common || region_length < 56U) {
                    serial_write("VANTAOS_VIRTIO_NET_MODERN_CAP_COMMON_INVALID\n");
                    return 0;
                }
                state.common = map_cap(pci, bar, offset, region_length);
                state.common_length = region_length;
                if (!state.common) {
                    serial_write("VANTAOS_VIRTIO_NET_MODERN_CAP_COMMON_MAP_FAIL\n");
                    return 0;
                }
            } else if (type == VNET_CAP_NOTIFY) {
                if (state.notify_region || length < 20U) {
                    serial_write("VANTAOS_VIRTIO_NET_MODERN_CAP_NOTIFY_INVALID\n");
                    return 0;
                }
                state.notify_multiplier = pci_read_config32(pci, (uint8_t)(at + 16U));
                state.notify_region = map_cap(pci, bar, offset, region_length);
                state.notify_length = region_length;
                if (!state.notify_region) {
                    serial_write("VANTAOS_VIRTIO_NET_MODERN_CAP_NOTIFY_MAP_FAIL\n");
                    return 0;
                }
                if (!state.notify_multiplier) {
                    serial_write("VANTAOS_VIRTIO_NET_MODERN_CAP_NOTIFY_MULTIPLIER_ZERO\n");
                    return 0;
                }
            } else if (type == VNET_CAP_DEVICE) {
                if (state.device_config || region_length < 6U) {
                    serial_write("VANTAOS_VIRTIO_NET_MODERN_CAP_DEVICE_INVALID\n");
                    return 0;
                }
                state.device_config = map_cap(pci, bar, offset, region_length);
                state.device_config_length = region_length;
                if (!state.device_config) {
                    serial_write("VANTAOS_VIRTIO_NET_MODERN_CAP_DEVICE_MAP_FAIL\n");
                    return 0;
                }
            }
        }
        at = next;
    }
    if (!state.common) serial_write("VANTAOS_VIRTIO_NET_MODERN_CAP_COMMON_MISSING\n");
    if (!state.notify_region) serial_write("VANTAOS_VIRTIO_NET_MODERN_CAP_NOTIFY_MISSING\n");
    if (!state.device_config) serial_write("VANTAOS_VIRTIO_NET_MODERN_CAP_DEVICE_MISSING\n");
    return state.common && state.notify_region && state.device_config;
}


#ifdef VANTAOS_SELF_TEST
static void vnet_modern_rx_selftest_handler(net_device_t *device, const uint8_t *frame,
                                            size_t length, uint16_t type,
                                            size_t header, void *context)
{
    (void)context;
    if (device != &state.netdev || !frame || type != 0x88B5U ||
        header > length || length - header < 4U)
        return;
    if (frame[header] == 0x56U && frame[header + 1U] == 0x41U &&
        frame[header + 2U] == 0x4EU && frame[header + 3U] == 0x54U)
        serial_write("VANTAOS_VIRTIO_NET_MODERN_RX_SELFTEST_OK\n");
}
#endif

static void release_queue(vnet_queue_t *q)
{
    if (!q) return;
    for (uint16_t i = 0; i < q->buffer_count; ++i)
        dma_free_page(&q->buffers[i]);
    if (q->ring_allocated)
        dma_free_page(&q->ring);
    *q = (vnet_queue_t){0};
}

static int allocate_queue(vnet_queue_t *q, uint16_t index)
{
    volatile uint8_t *c = state.common;
    wr16(c, 22U, index);
    uint16_t maximum = rd16(c, 24U);
    if (maximum < 8U) return 0;
    uint16_t size = 1U;
    while ((uint16_t)(size << 1U) && (uint16_t)(size << 1U) <= maximum &&
           (uint16_t)(size << 1U) <= VNET_QUEUE_LIMIT)
        size <<= 1U;
    q->size = size;
    if (!dma_alloc_page(&q->ring)) return 0;
    q->ring_allocated = 1U;
    uint8_t *base = (uint8_t *)q->ring.virtual_address;
    uint32_t desc_bytes = (uint32_t)size * (uint32_t)sizeof(vnet_desc_t);
    uint32_t avail_offset = desc_bytes;
    uint32_t used_offset = (avail_offset + 4U + (uint32_t)size * 2U + 3U) & ~3U;
    uint32_t end = used_offset + 4U + (uint32_t)size * (uint32_t)sizeof(vnet_used_elem_t);
    if (end > VNET_PAGE_SIZE) return 0;
    q->desc = (vnet_desc_t *)base;
    q->avail = (vnet_avail_t *)(base + avail_offset);
    q->used = (vnet_used_t *)(base + used_offset);
    for (uint16_t i = 0; i < size; ++i) {
        if (!dma_alloc_page(&q->buffers[i])) return 0;
        ++q->buffer_count;
    }
    wr16(c, 22U, index);
    wr16(c, 24U, size);
    wr16(c, 26U, 0xFFFFU);
    wr64(c, 32U, q->ring.physical_address);
    wr64(c, 40U, q->ring.physical_address + avail_offset);
    wr64(c, 48U, q->ring.physical_address + used_offset);
    uint16_t notify_offset = rd16(c, 30U);
    uint64_t byte_offset = (uint64_t)notify_offset * state.notify_multiplier;
    if (byte_offset + 2U > state.notify_length) return 0;
    q->notify = state.notify_region + byte_offset;
    wr16(c, 28U, 1U);
    return rd16(c, 28U) == 1U;
}

static void notify_queue(vnet_queue_t *q, uint16_t index)
{
    (void)q;
    wr16(state.common, 22U, index);
    wr16(q->notify, 0U, index);
}

static int vnet_modern_transmit(net_device_t *device, const uint8_t *frame, size_t length)
{
    if (device != &state.netdev || !state.ready || !frame ||
        length < NET_ETHERNET_HEADER_SIZE || length > NET_FRAME_MAX_SIZE ||
        length + virtio_header_size() > VNET_BUFFER_SIZE)
        return 0;
    spinlock_lock(&state.tx_lock);
    vnet_queue_t *q = &state.tx;
    uint16_t slot = (uint16_t)(q->avail_index % q->size);
    uint8_t *buffer = (uint8_t *)q->buffers[slot].virtual_address;
    size_t header_size = virtio_header_size();
    for (size_t i = 0; i < header_size; ++i) buffer[i] = 0U;
    for (size_t i = 0; i < length; ++i) buffer[header_size + i] = frame[i];
    q->desc[slot].address = q->buffers[slot].physical_address;
    q->desc[slot].length = (uint32_t)(header_size + length);
    q->desc[slot].flags = 0U;
    q->desc[slot].next = 0U;
    q->avail->ring[q->avail_index % q->size] = slot;
    ++q->avail_index;
    dma_barrier();
    q->avail->index = q->avail_index;
    dma_barrier();
    notify_queue(q, 1U);
    uint16_t expected = (uint16_t)(q->last_used + 1U);
    int completed = 0;
    for (uint32_t i = 0; i < VNET_WAIT_LIMIT; ++i) {
        dma_barrier();
        if (q->used->index == expected) {
            q->last_used = expected;
            completed = 1;
            break;
        }
        __asm__ volatile ("pause");
    }
    spinlock_unlock(&state.tx_lock);
    return completed;
}

static int vnet_modern_poll(void)
{
    if (!state.ready) return 0;
    vnet_queue_t *q = &state.rx;
    int received = 0;
    for (uint32_t budget = 0; budget < 64U; ++budget) {
        dma_barrier();
        if (q->last_used == q->used->index) break;
        vnet_used_elem_t element = q->used->ring[q->last_used % q->size];
        ++q->last_used;
        if (element.id >= q->size) continue;
        uint16_t slot = (uint16_t)element.id;
        uint8_t *buffer = (uint8_t *)q->buffers[slot].virtual_address;
        size_t header_size = virtio_header_size();
        if (element.length >= header_size + NET_ETHERNET_HEADER_SIZE &&
            element.length <= VNET_BUFFER_SIZE &&
            (!state.mergeable_rx ||
             rd16((volatile uint8_t *)buffer, VNET_HEADER_BASE_SIZE) == 1U)) {
            size_t length = element.length - header_size;
            if (length <= NET_FRAME_MAX_SIZE)
                (void)net_receive_frame(&state.netdev, buffer + header_size, length);
        }
        q->desc[slot].address = q->buffers[slot].physical_address;
        q->desc[slot].length = VNET_BUFFER_SIZE;
        q->desc[slot].flags = VNET_DESC_WRITE;
        q->desc[slot].next = 0U;
        q->avail->ring[q->avail_index % q->size] = slot;
        ++q->avail_index;
        dma_barrier();
        q->avail->index = q->avail_index;
        dma_barrier();
        notify_queue(q, 0U);
        ++received;
    }
    if (state.has_status && state.device_config_length >= 8U) {
        int up = (rd16(state.device_config, 6U) & 1U) != 0U;
        if ((int)state.netdev.link_up != up)
            (void)net_device_set_link(&state.netdev, up);
    }
    return received;
}


static void cleanup(void)
{
    if (state.common) wr8(state.common, 20U, 0U);
    state.ready = 0U;
#ifdef VANTAOS_SELF_TEST
    if (state.selftest_handler_registered) {
        (void)net_ethernet_handler_unregister(0x88B5U, vnet_modern_rx_selftest_handler, 0);
        state.selftest_handler_registered = 0U;
    }
#endif
    if (state.registered) {
        (void)net_device_unregister(&state.netdev);
        state.registered = 0U;
    }
    release_queue(&state.rx);
    release_queue(&state.tx);
    if (state.common) (void)mmio_unmap_region((void *)state.common, state.common_length);
    if (state.notify_region) (void)mmio_unmap_region((void *)state.notify_region, state.notify_length);
    if (state.device_config) (void)mmio_unmap_region((void *)state.device_config, state.device_config_length);
    state = (vnet_modern_state_t){0};
}

static net_pci_probe_result_t vnet_modern_probe(const struct pci_device *pci)
{
    const char *failure = "entry";
    if (!pci || state.ready || state.pci) return NET_PCI_PROBE_NOT_MINE;
    if (!pci_enable_memory_space(pci)) {
        serial_write("VANTAOS_VIRTIO_NET_MODERN_FAIL_MEMORY_SPACE\\n");
        return NET_PCI_PROBE_FAILED;
    }
    state.pci = pci;
    failure = "capabilities";
    if (!discover_capabilities(pci)) goto fail;
    failure = "status-reset";
    wr8(state.common, 20U, 0U);
    for (uint32_t i = 0; i < 100000U && rd8(state.common, 20U) != 0U; ++i)
        __asm__ volatile ("pause");
    wr8(state.common, 20U, VNET_STATUS_ACK);
    wr8(state.common, 20U, VNET_STATUS_ACK | VNET_STATUS_DRIVER);
    failure = "features";
    wr32(state.common, 0U, 0U);
    uint64_t features = rd32(state.common, 4U);
    wr32(state.common, 0U, 1U);
    features |= (uint64_t)rd32(state.common, 4U) << 32;
    if ((features & (VNET_F_MAC | VNET_F_VERSION_1)) != (VNET_F_MAC | VNET_F_VERSION_1)) goto fail;
    state.has_status = (features & VNET_F_STATUS) != 0U;
    state.mergeable_rx = (features & VNET_F_MRG_RXBUF) != 0U;
    wr32(state.common, 8U, 0U);
    wr32(state.common, 12U, (uint32_t)(VNET_F_MAC | (state.has_status ? VNET_F_STATUS : 0U) |
          (state.mergeable_rx ? VNET_F_MRG_RXBUF : 0U)));
    wr32(state.common, 8U, 1U);
    wr32(state.common, 12U, 1U);
    failure = "driver-ok";
    wr8(state.common, 20U, VNET_STATUS_ACK | VNET_STATUS_DRIVER | VNET_STATUS_FEATURES_OK);
    failure = "features-accepted";
    if (!(rd8(state.common, 20U) & VNET_STATUS_FEATURES_OK)) goto fail;
    failure = "queue-count-or-device-config";
    if (rd16(state.common, 18U) < 2U || state.device_config_length < 6U ||
        (state.has_status && state.device_config_length < 8U)) goto fail;
    failure = "bus-mastering";
    if (!pci_enable_bus_mastering(pci)) goto fail;
    failure = "queue-allocation";
    if (!allocate_queue(&state.rx, 0U) || !allocate_queue(&state.tx, 1U)) goto fail;
    uint8_t mac[6], any = 0U;
    for (uint32_t i = 0; i < 6U; ++i) { mac[i] = rd8(state.device_config, i); any |= mac[i]; }
    failure = "mac-address";
    if (!any || (mac[0] & 1U)) goto fail;
    for (uint16_t i = 0; i < state.rx.size; ++i) {
        state.rx.desc[i].address = state.rx.buffers[i].physical_address;
        state.rx.desc[i].length = VNET_BUFFER_SIZE;
        state.rx.desc[i].flags = VNET_DESC_WRITE;
        state.rx.desc[i].next = 0U;
        state.rx.avail->ring[i] = i;
    }
    state.rx.avail_index = state.rx.size;
    dma_barrier();
    state.rx.avail->index = state.rx.avail_index;
    dma_barrier();
    state.netdev = (net_device_t){0};
    state.netdev.name[0]='v'; state.netdev.name[1]='n'; state.netdev.name[2]='m'; state.netdev.name[3]='0';
    for (uint32_t i = 0; i < 6U; ++i) state.netdev.mac[i] = mac[i];
    state.netdev.mtu = NET_MTU_DEFAULT;
    state.netdev.link_up = state.has_status ? ((rd16(state.device_config, 6U) & 1U) != 0U) : 1U;
    state.netdev.ops = &vnet_modern_ops;
    state.netdev.driver_data = &state;
    spinlock_init(&state.tx_lock);
    failure = "net-register";
    if (!net_device_register(&state.netdev)) goto fail;
    state.registered = 1U;
#ifdef VANTAOS_SELF_TEST
    if (!net_device_configure_ipv4(&state.netdev, 0x0A00020FU, 0xFFFFFF00U, 0x0A000202U) ||
        !net_ethernet_handler_register(0x88B5U, vnet_modern_rx_selftest_handler, 0)) {
        
        goto fail;
    }
    state.selftest_handler_registered = 1U;
#endif
    wr8(state.common, 20U, VNET_STATUS_ACK | VNET_STATUS_DRIVER |
         VNET_STATUS_FEATURES_OK | VNET_STATUS_DRIVER_OK);
    if (!(rd8(state.common, 20U) & VNET_STATUS_DRIVER_OK)) goto fail;
    state.ready = 1U;
    notify_queue(&state.rx, 0U);
#ifdef VANTAOS_SELF_TEST
    uint8_t probe[18] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],0x88,0xB5,0x56,0x41,0x4E,0x54};
    if (net_device_transmit(&state.netdev, probe, sizeof(probe)))
        serial_write("VANTAOS_VIRTIO_NET_MODERN_TX_SELFTEST_OK\n");
    else
        serial_write("VANTAOS_VIRTIO_NET_MODERN_TX_SELFTEST_FAIL\n");
#endif
    serial_write("VANTAOS_VIRTIO_NET_MODERN_INITIALIZED\n");
    return NET_PCI_PROBE_BOUND;
fail:
    serial_write("VANTAOS_VIRTIO_NET_MODERN_FAIL_");
    serial_write(failure);
    serial_write("\\n");
    cleanup();
    return NET_PCI_PROBE_FAILED;
}

int virtio_net_modern_initialize(void)
{
    state = (vnet_modern_state_t){0};
    spinlock_init(&state.tx_lock);
    return net_pci_driver_register(&vnet_modern_driver);
}
