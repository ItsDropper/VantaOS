/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "xhci.h"

#include "paging.h"
#include "pci.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define XHCI_PCI_CLASS       0x0CU
#define XHCI_PCI_SUBCLASS    0x03U
#define XHCI_PCI_PROG_IF     0x30U

#define XHCI_MMIO_BASE       0xE0400000ULL
#define XHCI_MMIO_MAP_SIZE   0x00010000ULL
#define XHCI_MAX_PORTS       32U
#define XHCI_MAX_SLOTS       8U
#define XHCI_EP0_RING_TRBS   16U
#define XHCI_RING_TRBS       256U
#define XHCI_BULK_BUFFER_SIZE 4096U
#define XHCI_SCRATCHPAD_MAX  64U
#define XHCI_POLL_LIMIT      10000000U

#define XHCI_CAP_CAPLENGTH   0x00U
#define XHCI_CAP_HCSPARAMS1  0x04U
#define XHCI_CAP_HCSPARAMS2  0x08U
#define XHCI_CAP_HCCPARAMS1  0x10U
#define XHCI_CAP_DBOFF       0x14U
#define XHCI_CAP_RTSOFF      0x18U

#define XHCI_USBCMD          0x00U
#define XHCI_USBSTS          0x04U
#define XHCI_PAGESIZE        0x08U
#define XHCI_CRCR            0x18U
#define XHCI_DCBAAP           0x30U
#define XHCI_CONFIG           0x38U
#define XHCI_PORTSC_BASE      0x400U

#define XHCI_CMD_RUN          (1U << 0)
#define XHCI_CMD_RESET        (1U << 1)
#define XHCI_CMD_INTERRUPTS   (1U << 2)
#define XHCI_STS_HALTED       (1U << 0)
#define XHCI_STS_HOST_ERROR   (1U << 2)
#define XHCI_STS_NOT_READY    (1U << 11)
#define XHCI_PORT_CONNECTED   (1U << 0)
#define XHCI_PORT_ENABLED     (1U << 1)
#define XHCI_PORT_POWER       (1U << 9)
#define XHCI_PORT_RESET       (1U << 4)
#define XHCI_PORT_WARM_RESET  (1U << 31)
#define XHCI_PORT_CHANGE_BITS ((1U << 17) | (1U << 18) | (1U << 19) | \
                               (1U << 20) | (1U << 21) | (1U << 22) | (1U << 23))

#define XHCI_TRB_CYCLE        (1U << 0)
#define XHCI_TRB_TYPE_SHIFT   10U
#define XHCI_TRB_TYPE_LINK    6U
#define XHCI_TRB_TYPE_EVENT_TRANSFER           32U
#define XHCI_TRB_TYPE_EVENT_COMMAND_COMPLETION 33U
#define XHCI_TRB_TYPE_EVENT_PORT_STATUS       34U
#define XHCI_TRB_TYPE_CMD_ENABLE_SLOT         9U
#define XHCI_TRB_TYPE_CMD_ADDRESS_DEVICE      11U
#define XHCI_TRB_TYPE_CMD_EVALUATE_CONTEXT    13U
#define XHCI_TRB_TYPE_CMD_NOOP                23U

typedef struct __attribute__((aligned(16))) {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} xhci_trb_t;

typedef struct __attribute__((aligned(16))) {
    uint64_t address;
    uint32_t size;
    uint32_t reserved;
} xhci_erst_entry_t;

/*
 * DMA structures are statically allocated in the low kernel image. The current
 * kernel identity maps its first GiB, so their virtual addresses are also
 * usable physical addresses. Initialization verifies that invariant before
 * enabling bus mastering rather than silently programming invalid DMA targets.
 */
static uint64_t dcbaa[256] __attribute__((aligned(64)));
static xhci_trb_t command_ring[XHCI_RING_TRBS] __attribute__((aligned(65536)));
static xhci_trb_t event_ring[XHCI_RING_TRBS] __attribute__((aligned(65536)));
static xhci_erst_entry_t erst[1] __attribute__((aligned(64)));
static uint64_t scratchpad_array[XHCI_SCRATCHPAD_MAX] __attribute__((aligned(64)));
static uint8_t input_contexts[XHCI_MAX_SLOTS][33U * 64U]
    __attribute__((aligned(64)));
static uint8_t device_contexts[XHCI_MAX_SLOTS][32U * 64U]
    __attribute__((aligned(64)));
static xhci_trb_t ep0_rings[XHCI_MAX_SLOTS][XHCI_EP0_RING_TRBS]
    __attribute__((aligned(64)));
static uint32_t ep0_enqueue[XHCI_MAX_SLOTS];
static uint32_t ep0_cycle[XHCI_MAX_SLOTS];
static uint8_t slot_active[XHCI_MAX_SLOTS];
static uint8_t slot_port[XHCI_MAX_SLOTS];
static uint8_t slot_speed[XHCI_MAX_SLOTS];
static uint16_t slot_ep0_packet[XHCI_MAX_SLOTS];
static uint32_t context_size = 32U;
static uint8_t scratchpad_buffers[XHCI_SCRATCHPAD_MAX][4096]
    __attribute__((aligned(4096)));

static volatile uint8_t* registers;
static volatile uint8_t* operational;
static volatile uint8_t* runtime;
static volatile uint8_t* doorbells;
static uint32_t max_ports;
static uint32_t max_slots;
static uint32_t command_enqueue;
static uint32_t command_cycle = 1;
static uint32_t event_dequeue;
static uint32_t event_cycle = 1;
static uint32_t port_event_mask;
static uint64_t completed_command_pointer;
static uint32_t completed_command_code;
static uint32_t completed_command_slot;
static uint32_t command_completion_sequence;
static uint64_t completed_transfer_pointer;
static uint32_t completed_transfer_code;
static uint32_t completed_transfer_status;
static uint32_t completed_transfer_slot;
static uint32_t transfer_completion_sequence;
static xhci_trb_t interrupt_rings[XHCI_MAX_SLOTS][XHCI_EP0_RING_TRBS]
    __attribute__((aligned(64)));
static uint8_t interrupt_dma[XHCI_MAX_SLOTS][64] __attribute__((aligned(64)));
static uint8_t interrupt_pending[XHCI_MAX_SLOTS][64] __attribute__((aligned(64)));
static uint64_t interrupt_expected_trb[XHCI_MAX_SLOTS];
static uint8_t transfer_event_observed;
static uint8_t hid_completion_observed[XHCI_MAX_SLOTS];
static uint8_t hid_report_observed[XHCI_MAX_SLOTS];
static uint8_t hid_transfer_submitted[XHCI_MAX_SLOTS];
static uint8_t hid_transfer_event_observed[XHCI_MAX_SLOTS];
static uint8_t hid_completion_mismatch_observed[XHCI_MAX_SLOTS];
static uint32_t interrupt_enqueue[XHCI_MAX_SLOTS];
static uint32_t interrupt_cycle[XHCI_MAX_SLOTS];
static uint32_t interrupt_dci[XHCI_MAX_SLOTS];
static uint32_t interrupt_packet[XHCI_MAX_SLOTS];
static uint32_t interrupt_pending_length[XHCI_MAX_SLOTS];
static uint8_t interrupt_configured[XHCI_MAX_SLOTS];
static uint8_t interrupt_in_flight[XHCI_MAX_SLOTS];
static uint8_t interrupt_report_ready[XHCI_MAX_SLOTS];
static uint8_t interrupt_endpoint_address[XHCI_MAX_SLOTS];
static xhci_trb_t bulk_rings[XHCI_MAX_SLOTS][2][XHCI_EP0_RING_TRBS]
    __attribute__((aligned(64)));
static uint8_t bulk_dma[XHCI_MAX_SLOTS][2][XHCI_BULK_BUFFER_SIZE]
    __attribute__((aligned(64)));
static uint32_t bulk_enqueue[XHCI_MAX_SLOTS][2];
static uint32_t bulk_cycle[XHCI_MAX_SLOTS][2];
static uint32_t bulk_dci[XHCI_MAX_SLOTS][2];
static uint16_t bulk_packet[XHCI_MAX_SLOTS][2];
static uint8_t bulk_configured[XHCI_MAX_SLOTS][2];
static uint8_t bulk_in_flight[XHCI_MAX_SLOTS][2];
static uint8_t bulk_done[XHCI_MAX_SLOTS][2];
static uint64_t bulk_expected_trb[XHCI_MAX_SLOTS][2];
static uint32_t bulk_completion_code[XHCI_MAX_SLOTS][2];
static uint32_t bulk_completion_status[XHCI_MAX_SLOTS][2];
static int controller_online;
static int initialized;

static uint32_t mmio_read32(volatile uint8_t* base, uint32_t offset)
{
    return *(volatile uint32_t*)(base + offset);
}

static void mmio_write32(volatile uint8_t* base, uint32_t offset,
                         uint32_t value)
{
    *(volatile uint32_t*)(base + offset) = value;
}

static void mmio_write64(volatile uint8_t* base, uint32_t offset,
                         uint64_t value)
{
    mmio_write32(base, offset, (uint32_t)value);
    mmio_write32(base, offset + 4U, (uint32_t)(value >> 32));
}

static int wait_register(volatile uint8_t* base, uint32_t offset,
                         uint32_t mask, uint32_t expected)
{
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        if ((mmio_read32(base, offset) & mask) == expected)
            return 1;
        __asm__ volatile ("pause");
    }
    return 0;
}

static int dma_address(const void* pointer, uint64_t* address_out)
{
    uintptr_t virtual_address = (uintptr_t)pointer;
    uintptr_t physical_address = paging_get_physical(virtual_address);
    if (!address_out || physical_address == 0 ||
        physical_address >= 0x100000000ULL)
        return 0;
    *address_out = (uint64_t)physical_address;
    return 1;
}

static int map_controller_bar(uint64_t physical_bar)
{
    uint64_t page = physical_bar & ~((uint64_t)PAGE_SIZE - 1U);
    uint64_t offset = physical_bar - page;
    if (offset + XHCI_MMIO_MAP_SIZE > 0x400000ULL)
        return 0;

    for (uint64_t mapped = 0; mapped < XHCI_MMIO_MAP_SIZE;
         mapped += PAGE_SIZE) {
        if (!paging_map_mmio_page((uintptr_t)(XHCI_MMIO_BASE + mapped),
                                  (uintptr_t)(page + mapped)))
            return 0;
    }

    registers = (volatile uint8_t*)(uintptr_t)(XHCI_MMIO_BASE + offset);
    return 1;
}

static void handle_interrupt_completion(uint32_t slot_id, uint32_t dci,
                                       uint64_t trb_pointer, uint32_t status);

static void consume_event_ring(void)
{
    for (;;) {
        xhci_trb_t* event = &event_ring[event_dequeue];
        uint32_t control = __atomic_load_n(&event->control, __ATOMIC_ACQUIRE);
        if ((control & XHCI_TRB_CYCLE) != event_cycle)
            return;

        uint32_t type = (control >> XHCI_TRB_TYPE_SHIFT) & 0x3FU;
        if (type == XHCI_TRB_TYPE_EVENT_PORT_STATUS) {
            uint32_t port_id = (control >> 24) & 0xFFU;
            if (port_id >= 1U && port_id <= XHCI_MAX_PORTS)
                port_event_mask |= 1U << (port_id - 1U);
        } else if (type == XHCI_TRB_TYPE_EVENT_COMMAND_COMPLETION) {
            completed_command_pointer = event->parameter;
            completed_command_code = (event->status >> 24) & 0xFFU;
            completed_command_slot = (control >> 24) & 0xFFU;
            __atomic_add_fetch(&command_completion_sequence, 1U,
                               __ATOMIC_RELEASE);
        } else if (type == XHCI_TRB_TYPE_EVENT_TRANSFER) {
            if (!transfer_event_observed) {
                transfer_event_observed = 1;
                serial_write("VANTAOS_XHCI_TRANSFER_EVENT_SEEN\n");
            }
            completed_transfer_pointer = event->parameter;
            completed_transfer_code = (event->status >> 24) & 0xFFU;
            completed_transfer_status = event->status;
            completed_transfer_slot = (control >> 24) & 0xFFU;
            if (completed_transfer_slot > 0U &&
                completed_transfer_slot <= XHCI_MAX_SLOTS &&
                interrupt_configured[completed_transfer_slot - 1U] &&
                !hid_transfer_event_observed[completed_transfer_slot - 1U]) {
                hid_transfer_event_observed[completed_transfer_slot - 1U] = 1;
                serial_write("VANTAOS_XHCI_HID_TRANSFER_EVENT_SEEN\n");
            }
            if (completed_transfer_slot > 0U &&
                completed_transfer_slot <= XHCI_MAX_SLOTS) {
                uint32_t transfer_index = completed_transfer_slot - 1U;
                for (uint32_t direction = 0; direction < 2U; ++direction) {
                    if (bulk_in_flight[transfer_index][direction] &&
                        bulk_expected_trb[transfer_index][direction] ==
                            completed_transfer_pointer &&
                        bulk_dci[transfer_index][direction] ==
                            ((control >> 16) & 0x1FU)) {
                        bulk_completion_code[transfer_index][direction] =
                            completed_transfer_code;
                        bulk_completion_status[transfer_index][direction] =
                            event->status;
                        bulk_done[transfer_index][direction] = 1U;
                    }
                }
            }
            __atomic_add_fetch(&transfer_completion_sequence, 1U,
                               __ATOMIC_RELEASE);
            handle_interrupt_completion(completed_transfer_slot,
                                        (control >> 16) & 0x1FU,
                                        completed_transfer_pointer,
                                        event->status);
        }

        event_dequeue++;
        if (event_dequeue == XHCI_RING_TRBS) {
            event_dequeue = 0;
            event_cycle ^= 1U;
        }

        /* EHB is bit 3; ERDP bits 3:0 are reserved except EHB. */
        uint64_t event_physical = (uint64_t)(uintptr_t)&event_ring[event_dequeue];
        mmio_write64(runtime, 0x38U, event_physical | (1U << 3));
    }
}

int xhci_submit_command(uint32_t type, uint64_t parameter,
                         uint32_t status, uint32_t control_flags,
                         uint32_t* completion_code, uint32_t* slot_id)
{
    if (!controller_online || type == XHCI_TRB_TYPE_LINK ||
        command_enqueue >= XHCI_RING_TRBS - 1U)
        return 0;

    xhci_trb_t* trb = &command_ring[command_enqueue];
    uint64_t command_physical;
    if (!dma_address(trb, &command_physical))
        return 0;

    uint32_t previous_sequence = __atomic_load_n(
        &command_completion_sequence, __ATOMIC_ACQUIRE);
    trb->parameter = parameter;
    trb->status = status;
    trb->control = (type << XHCI_TRB_TYPE_SHIFT) |
                   (control_flags & 0xFF1F03FEU) |
                   (command_cycle ? XHCI_TRB_CYCLE : 0U);
    __atomic_thread_fence(__ATOMIC_RELEASE);

    command_enqueue++;
    if (command_enqueue == XHCI_RING_TRBS - 1U) {
        xhci_trb_t* link = &command_ring[XHCI_RING_TRBS - 1U];
        uint64_t ring_physical;
        if (!dma_address(command_ring, &ring_physical))
            return 0;
        link->parameter = ring_physical;
        link->status = 0;
        link->control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                        (1U << 1) |
                        (command_cycle ? XHCI_TRB_CYCLE : 0U);
        command_enqueue = 0;
        command_cycle ^= 1U;
    }

    mmio_write32(doorbells, 0, 0);
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        consume_event_ring();
        uint32_t sequence = __atomic_load_n(
            &command_completion_sequence, __ATOMIC_ACQUIRE);
        if (sequence != previous_sequence &&
            completed_command_pointer == command_physical) {
            if (completion_code)
                *completion_code = completed_command_code;
            if (slot_id)
                *slot_id = completed_command_slot;
            return completed_command_code == 1U;
        }
        if (mmio_read32(operational, XHCI_USBSTS) & XHCI_STS_HOST_ERROR)
            return 0;
        __asm__ volatile ("pause");
    }
    return 0;
}

static uint32_t* input_context_word(uint32_t slot_index, uint32_t context_index)
{
    return (uint32_t*)(input_contexts[slot_index] +
                       context_index * context_size);
}

static int ep0_ring_push(uint32_t slot_index, uint64_t parameter,
                         uint32_t status, uint32_t control,
                         uint64_t* trb_physical_out)
{
    if (ep0_enqueue[slot_index] >= XHCI_EP0_RING_TRBS - 1U)
        return 0;
    xhci_trb_t* trb = &ep0_rings[slot_index][ep0_enqueue[slot_index]];
    uint64_t physical;
    if (!dma_address(trb, &physical))
        return 0;
    trb->parameter = parameter;
    trb->status = status;
    trb->control = control | (ep0_cycle[slot_index] ? XHCI_TRB_CYCLE : 0U);
    if (trb_physical_out)
        *trb_physical_out = physical;
    ep0_enqueue[slot_index]++;
    if (ep0_enqueue[slot_index] == XHCI_EP0_RING_TRBS - 1U) {
        xhci_trb_t* link = &ep0_rings[slot_index][XHCI_EP0_RING_TRBS - 1U];
        uint64_t ring_physical;
        if (!dma_address(ep0_rings[slot_index], &ring_physical))
            return 0;
        link->parameter = ring_physical;
        link->status = 0;
        link->control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                        (1U << 1) |
                        (ep0_cycle[slot_index] ? XHCI_TRB_CYCLE : 0U);
        ep0_enqueue[slot_index] = 0;
        ep0_cycle[slot_index] ^= 1U;
    }
    return 1;
}

int xhci_enable_device(uint32_t port, uint32_t speed, uint16_t ep0_packet,
                      uint32_t* slot_id_out)
{
    if (!controller_online || !slot_id_out || port == 0U ||
        port > max_ports || speed == 0U || ep0_packet == 0U)
        return 0;
    uint32_t completion = 0, slot_id = 0;
    if (!xhci_submit_command(XHCI_TRB_TYPE_CMD_ENABLE_SLOT, 0, 0, 0,
                             &completion, &slot_id) ||
        slot_id == 0U || slot_id > XHCI_MAX_SLOTS || slot_id > max_slots)
        return 0;

    uint32_t index = slot_id - 1U;
    for (uint32_t i = 0; i < 33U * 64U; ++i)
        input_contexts[index][i] = 0;
    for (uint32_t i = 0; i < 32U * 64U; ++i)
        device_contexts[index][i] = 0;
    for (uint32_t i = 0; i < XHCI_EP0_RING_TRBS; ++i) {
        ep0_rings[index][i].parameter = 0;
        ep0_rings[index][i].status = 0;
        ep0_rings[index][i].control = 0;
    }
    ep0_enqueue[index] = 0;
    ep0_cycle[index] = 1;
    uint64_t ring_physical, device_context_physical, input_context_physical;
    if (!dma_address(ep0_rings[index], &ring_physical) ||
        !dma_address(device_contexts[index], &device_context_physical) ||
        !dma_address(input_contexts[index], &input_context_physical)) {
        (void)xhci_submit_command(10U, 0, 0, slot_id << 24, &completion, 0);
        return 0;
    }
    ep0_rings[index][XHCI_EP0_RING_TRBS - 1U].parameter = ring_physical;
    ep0_rings[index][XHCI_EP0_RING_TRBS - 1U].status = 0;
    ep0_rings[index][XHCI_EP0_RING_TRBS - 1U].control =
        (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | (1U << 1) | 1U;
    dcbaa[slot_id] = device_context_physical;

    uint32_t* control = input_context_word(index, 0);
    uint32_t* slot = input_context_word(index, 1);
    uint32_t* ep0 = input_context_word(index, 2);
    control[1] = (1U << 0) | (1U << 1);
    slot[0] = ((speed & 0xFU) << 20) | (1U << 27);
    slot[1] = (port & 0xFFU) << 16;
    ep0[1] = (3U << 1) | (4U << 3) | ((uint32_t)ep0_packet << 16);
    ep0[2] = (uint32_t)ring_physical | 1U;
    ep0[3] = (uint32_t)(ring_physical >> 32);

    if (!xhci_submit_command(XHCI_TRB_TYPE_CMD_ADDRESS_DEVICE,
                             input_context_physical, 0, slot_id << 24,
                             &completion, 0)) {
        (void)xhci_submit_command(10U, 0, 0, slot_id << 24, &completion, 0);
        dcbaa[slot_id] = 0;
        slot_active[index] = 0;
        return 0;
    }
    slot_active[index] = 1;
    slot_port[index] = (uint8_t)port;
    slot_speed[index] = (uint8_t)speed;
    slot_ep0_packet[index] = ep0_packet;
    *slot_id_out = slot_id;
    return 1;
}

int xhci_control_transfer(uint32_t slot_id, const uint8_t setup[8],
                          void* data, uint16_t length)
{
    if (!controller_online || !setup || slot_id == 0U ||
        slot_id > XHCI_MAX_SLOTS || !slot_active[slot_id - 1U] ||
        (length != 0U && !data))
        return 0;
    uint32_t index = slot_id - 1U;
    uint64_t setup_value = 0;
    const uint8_t* setup_bytes = setup;
    for (uint32_t i = 0; i < 8U; ++i)
        setup_value |= (uint64_t)setup_bytes[i] << (i * 8U);
    uint32_t request_type = (setup[0] & 0x80U) ? 3U : 2U;
    if (length == 0U)
        request_type = 0U;
    uint32_t setup_control = (2U << XHCI_TRB_TYPE_SHIFT) |
                             (1U << 6) | (1U << 4) |
                             (request_type << 16);
    if (!ep0_ring_push(index, setup_value, 8U, setup_control, 0))
        return 0;

    int data_in = (setup[0] & 0x80U) != 0U;
    if (length != 0U) {
        uint64_t data_physical;
        if (!dma_address(data, &data_physical))
            return 0;
        uint32_t data_control = (3U << XHCI_TRB_TYPE_SHIFT) |
                                (1U << 4) |
                                (data_in ? (1U << 16) : 0U);
        if (!ep0_ring_push(index, data_physical, length, data_control, 0))
            return 0;
    }

    uint64_t status_physical;
    uint32_t status_in = (length == 0U || !data_in) ? 1U : 0U;
    uint32_t status_control = (4U << XHCI_TRB_TYPE_SHIFT) | (1U << 5) |
                              (status_in << 16);
    if (!ep0_ring_push(index, 0, 0, status_control, &status_physical))
        return 0;
    uint32_t previous_sequence = __atomic_load_n(
        &transfer_completion_sequence, __ATOMIC_ACQUIRE);
    mmio_write32(doorbells, slot_id * 4U, 1U);
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        consume_event_ring();
        uint32_t sequence = __atomic_load_n(
            &transfer_completion_sequence, __ATOMIC_ACQUIRE);
        if (sequence != previous_sequence &&
            completed_transfer_pointer == status_physical &&
            completed_transfer_slot == slot_id)
            return completed_transfer_code == 1U;
        if (mmio_read32(operational, XHCI_USBSTS) & XHCI_STS_HOST_ERROR)
            return 0;
        __asm__ volatile ("pause");
    }
    return 0;
}

int xhci_disable_device(uint32_t slot_id)
{
    if (!controller_online || slot_id == 0U || slot_id > XHCI_MAX_SLOTS ||
        !slot_active[slot_id - 1U])
        return 0;
    uint32_t completion = 0;
    if (!xhci_submit_command(10U, 0, 0, slot_id << 24, &completion, 0))
        return 0;
    dcbaa[slot_id] = 0;
    slot_active[slot_id - 1U] = 0;
    interrupt_configured[slot_id - 1U] = 0;
    interrupt_in_flight[slot_id - 1U] = 0;
    interrupt_report_ready[slot_id - 1U] = 0;
    for (uint32_t direction = 0; direction < 2U; ++direction) {
        bulk_configured[slot_id - 1U][direction] = 0;
        bulk_in_flight[slot_id - 1U][direction] = 0;
        bulk_done[slot_id - 1U][direction] = 0;
        bulk_expected_trb[slot_id - 1U][direction] = 0;
    }
    return 1;
}
int xhci_set_ep0_packet(uint32_t slot_id, uint16_t packet_size)
{
    if (!controller_online || slot_id == 0U || slot_id > XHCI_MAX_SLOTS ||
        !slot_active[slot_id - 1U] || packet_size == 0U)
        return 0;
    uint32_t index = slot_id - 1U;
    for (uint32_t i = 0; i < 33U * 64U; ++i)
        input_contexts[index][i] = 0;
    uint64_t ring_physical, input_physical;
    if (!dma_address(ep0_rings[index], &ring_physical) ||
        !dma_address(input_contexts[index], &input_physical))
        return 0;
    uint32_t* control = input_context_word(index, 0);
    uint32_t* ep0 = input_context_word(index, 2);
    control[1] = 1U << 1;
    ep0[1] = (3U << 1) | (4U << 3) | ((uint32_t)packet_size << 16);
    ep0[2] = (uint32_t)ring_physical | 1U;
    ep0[3] = (uint32_t)(ring_physical >> 32);
    uint32_t completion = 0;
    if (!xhci_submit_command(XHCI_TRB_TYPE_CMD_EVALUATE_CONTEXT,
                             input_physical, 0, slot_id << 24,
                             &completion, 0))
        return 0;
    slot_ep0_packet[index] = packet_size;
    return 1;
}

static int interrupt_ring_push(uint32_t index, uint64_t parameter,
                               uint32_t status, uint32_t control,
                               uint64_t* trb_physical_out)
{
    if (interrupt_enqueue[index] >= XHCI_EP0_RING_TRBS - 1U)
        return 0;
    xhci_trb_t* trb = &interrupt_rings[index][interrupt_enqueue[index]];
    uint64_t physical;
    if (!dma_address(trb, &physical))
        return 0;
    trb->parameter = parameter;
    trb->status = status;
    trb->control = control | (interrupt_cycle[index] ? XHCI_TRB_CYCLE : 0U);
    if (trb_physical_out)
        *trb_physical_out = physical;
    interrupt_enqueue[index]++;
    if (interrupt_enqueue[index] == XHCI_EP0_RING_TRBS - 1U) {
        xhci_trb_t* link = &interrupt_rings[index][XHCI_EP0_RING_TRBS - 1U];
        uint64_t ring_physical;
        if (!dma_address(interrupt_rings[index], &ring_physical))
            return 0;
        link->parameter = ring_physical;
        link->status = 0;
        link->control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                        (1U << 1) |
                        (interrupt_cycle[index] ? XHCI_TRB_CYCLE : 0U);
        interrupt_enqueue[index] = 0;
        interrupt_cycle[index] ^= 1U;
    }
    return 1;
}

static int submit_interrupt_trb(uint32_t index)
{
    if (interrupt_in_flight[index] || interrupt_report_ready[index])
        return 1;
    uint64_t data_physical, trb_physical;
    if (!dma_address(interrupt_dma[index], &data_physical))
        return 0;
    /* Interrupt-IN reports may complete as short packets. Request an event
       for those packets, and publish the fully initialized TRB before ringing
       the controller doorbell. */
    if (!interrupt_ring_push(index, data_physical, interrupt_packet[index],
                             (1U << XHCI_TRB_TYPE_SHIFT) | (1U << 5) |
                             (1U << 2), &trb_physical))
        return 0;
    interrupt_expected_trb[index] = trb_physical;
    interrupt_in_flight[index] = 1;
    if (!hid_transfer_submitted[index]) {
        hid_transfer_submitted[index] = 1;
        serial_write("VANTAOS_XHCI_HID_TRANSFER_SUBMITTED\n");
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);
    mmio_write32(doorbells, ((index + 1U) * 4U), interrupt_dci[index]);
    return 1;
}

static void handle_interrupt_completion(uint32_t slot_id, uint32_t dci,
                                       uint64_t trb_pointer, uint32_t status)
{
    if (slot_id == 0U || slot_id > XHCI_MAX_SLOTS)
        return;
    uint32_t index = slot_id - 1U;
    if (!interrupt_configured[index])
        return;
    if (!interrupt_in_flight[index] || dci != interrupt_dci[index] ||
        trb_pointer != interrupt_expected_trb[index]) {
        if (!hid_completion_mismatch_observed[index]) {
            hid_completion_mismatch_observed[index] = 1;
            serial_write("VANTAOS_XHCI_HID_COMPLETION_MISMATCH\n");
        }
        return;
    }
    if (!hid_completion_observed[index]) {
        hid_completion_observed[index] = 1;
        serial_write("VANTAOS_XHCI_HID_COMPLETION_MATCHED\n");
    }
    interrupt_in_flight[index] = 0;
    uint32_t completion = (status >> 24) & 0xFFU;
    uint32_t residual = status & 0xFFFFFFU;
    if ((completion == 1U || completion == 13U) &&
        residual <= interrupt_packet[index]) {
        uint32_t actual = interrupt_packet[index] - residual;
        for (uint32_t i = 0; i < actual && i < sizeof(interrupt_pending[index]); ++i)
            interrupt_pending[index][i] = interrupt_dma[index][i];
        interrupt_pending_length[index] = actual;
        interrupt_report_ready[index] = 1;
        if (!hid_report_observed[index]) {
            hid_report_observed[index] = 1;
            serial_write("VANTAOS_XHCI_HID_REPORT_RECEIVED\n");
        }
    }
}

int xhci_configure_interrupt_in(uint32_t slot_id, uint8_t endpoint_address,
                                uint16_t packet_size, uint8_t interval)
{
    if (!controller_online || slot_id == 0U || slot_id > XHCI_MAX_SLOTS ||
        !slot_active[slot_id - 1U] || (endpoint_address & 0x80U) == 0U ||
        packet_size == 0U || packet_size > sizeof(interrupt_dma[0]))
        return 0;
    uint32_t index = slot_id - 1U;
    uint32_t endpoint_number = endpoint_address & 0x0FU;
    uint32_t dci = endpoint_number * 2U + 1U;
    if (endpoint_number == 0U || dci >= 31U)
        return 0;
    for (uint32_t i = 0; i < 33U * 64U; ++i)
        input_contexts[index][i] = 0;
    for (uint32_t i = 0; i < XHCI_EP0_RING_TRBS; ++i) {
        interrupt_rings[index][i].parameter = 0;
        interrupt_rings[index][i].status = 0;
        interrupt_rings[index][i].control = 0;
    }
    interrupt_enqueue[index] = 0;
    interrupt_cycle[index] = 1;
    uint64_t ring_physical, input_physical;
    if (!dma_address(interrupt_rings[index], &ring_physical) ||
        !dma_address(input_contexts[index], &input_physical))
        return 0;
    interrupt_rings[index][XHCI_EP0_RING_TRBS - 1U].parameter = ring_physical;
    interrupt_rings[index][XHCI_EP0_RING_TRBS - 1U].status = 0;
    interrupt_rings[index][XHCI_EP0_RING_TRBS - 1U].control =
        (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | (1U << 1) | 1U;
    uint32_t* control = input_context_word(index, 0);
    uint32_t* slot = input_context_word(index, 1);
    uint32_t* endpoint = input_context_word(index, dci + 1U);
    /* Input Control Context Add Context Flags use bit DCI, not DCI + 1.
       The input-context array places endpoint DCI at context index DCI + 1,
       but the add-flags bitmap has bit 0 for Slot and bit DCI for Endpoint. */
    control[1] = 1U | (1U << dci);

    /*
     * Configure Endpoint requires the existing Slot Context to be carried
     * forward. Rebuilding only speed/port/context-entry fields zeroed the
     * assigned USB device address in DW3, so the command succeeded but later
     * interrupt-IN transfers never received replies from the device.
     */
    const uint32_t* current_slot =
        (const uint32_t*)(const void*)device_contexts[index];
    for (uint32_t word = 0; word < context_size / sizeof(uint32_t); ++word)
        slot[word] = current_slot[word];
    uint32_t existing_entries = (slot[0] >> 27) & 0x1FU;
    uint32_t required_entries = dci > existing_entries
        ? dci : existing_entries;
    if (required_entries < 1U)
        required_entries = 1U;
    slot[0] = (slot[0] & ~(0x1FU << 27)) |
              (required_entries << 27);
    uint32_t interval_value = 0;
    if (slot_speed[index] <= 3U) {
        uint32_t value = interval > 0U ? interval : 1U;
        interval_value = 0;
        while ((1U << interval_value) < value && interval_value < 7U)
            interval_value++;
        interval_value += 3U;
    } else {
        interval_value = interval > 0U ? (uint32_t)interval - 1U : 0U;
    }
    if (interval_value > 15U)
        interval_value = 15U;
    endpoint[0] = interval_value << 16;
    endpoint[1] = (3U << 1) | (7U << 3) | ((uint32_t)packet_size << 16);
    endpoint[2] = (uint32_t)ring_physical | 1U;
    endpoint[3] = (uint32_t)(ring_physical >> 32);
    /* Endpoint Context DW4: Average TRB Length in bits 0..15 and
       Max ESIT Payload Low in bits 16..31. Both must be populated for
       periodic interrupt endpoints; otherwise the controller may never
       schedule HID interrupt-IN transfers despite successful configuration. */
    endpoint[4] = (uint32_t)packet_size |
                  ((uint32_t)packet_size << 16);
    uint32_t completion = 0;
    if (!xhci_submit_command(12U, input_physical, 0, slot_id << 24,
                             &completion, 0))
        return 0;
    interrupt_dci[index] = dci;
    interrupt_packet[index] = packet_size;
    interrupt_endpoint_address[index] = endpoint_address;
    interrupt_configured[index] = 1;
    interrupt_in_flight[index] = 0;
    interrupt_report_ready[index] = 0;
    return 1;
}


/*
 * Configure one USB bulk endpoint. Direction is encoded by endpoint address;
 * xHCI DCI is 2*endpoint for OUT and 2*endpoint+1 for IN.
 */
int xhci_configure_bulk_endpoint(uint32_t slot_id, uint8_t endpoint_address,
                                 uint16_t packet_size)
{
    if (!controller_online || slot_id == 0U || slot_id > XHCI_MAX_SLOTS ||
        !slot_active[slot_id - 1U] || (endpoint_address & 0x0FU) == 0U ||
        packet_size == 0U || packet_size > XHCI_BULK_BUFFER_SIZE)
        return 0;
    uint32_t index = slot_id - 1U;
    uint32_t endpoint_number = endpoint_address & 0x0FU;
    uint32_t direction = (endpoint_address & 0x80U) != 0U ? 1U : 0U;
    uint32_t dci = endpoint_number * 2U + direction;
    if (dci >= 31U)
        return 0;
    for (uint32_t i = 0; i < 33U * 64U; ++i)
        input_contexts[index][i] = 0;
    for (uint32_t i = 0; i < XHCI_EP0_RING_TRBS; ++i) {
        bulk_rings[index][direction][i].parameter = 0;
        bulk_rings[index][direction][i].status = 0;
        bulk_rings[index][direction][i].control = 0;
    }
    bulk_enqueue[index][direction] = 0;
    bulk_cycle[index][direction] = 1;
    uint64_t ring_physical, input_physical;
    if (!dma_address(bulk_rings[index][direction], &ring_physical) ||
        !dma_address(input_contexts[index], &input_physical))
        return 0;
    bulk_rings[index][direction][XHCI_EP0_RING_TRBS - 1U].parameter = ring_physical;
    bulk_rings[index][direction][XHCI_EP0_RING_TRBS - 1U].status = 0;
    bulk_rings[index][direction][XHCI_EP0_RING_TRBS - 1U].control =
        (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | (1U << 1) | 1U;

    uint32_t* control = input_context_word(index, 0);
    uint32_t* slot = input_context_word(index, 1);
    uint32_t* endpoint = input_context_word(index, dci + 1U);
    control[1] = 1U | (1U << dci);
    const uint32_t* current_slot =
        (const uint32_t*)(const void*)device_contexts[index];
    for (uint32_t word = 0; word < context_size / sizeof(uint32_t); ++word)
        slot[word] = current_slot[word];
    uint32_t existing_entries = (slot[0] >> 27) & 0x1FU;
    uint32_t required_entries = dci > existing_entries
        ? dci : existing_entries;
    if (required_entries < 1U)
        required_entries = 1U;
    slot[0] = (slot[0] & ~(0x1FU << 27)) |
              (required_entries << 27);

    /* Bulk endpoints use error-count=3 and endpoint type 2 (IN) / 6 (OUT). */
    endpoint[0] = 0U;
    endpoint[1] = (3U << 1) |
                  ((direction ? 2U : 6U) << 3) |
                  ((uint32_t)packet_size << 16);
    endpoint[2] = (uint32_t)ring_physical | 1U;
    endpoint[3] = (uint32_t)(ring_physical >> 32);
    endpoint[4] = (uint32_t)packet_size;
    uint32_t completion = 0;
    if (!xhci_submit_command(12U, input_physical, 0, slot_id << 24,
                             &completion, 0))
        return 0;
    bulk_dci[index][direction] = dci;
    bulk_packet[index][direction] = packet_size;
    bulk_configured[index][direction] = 1U;
    return 1;
}

/*
 * Synchronous, bounded bulk transfer for USB network-device bring-up.
 * The caller's buffer is copied through controller-owned DMA memory. Maximum
 * transfer is deliberately capped; large firmware images must be chunked.
 */
int xhci_bulk_transfer(uint32_t slot_id, uint8_t endpoint_address,
                       void* buffer, uint32_t length, uint32_t* actual_length)
{
    if (!controller_online || slot_id == 0U || slot_id > XHCI_MAX_SLOTS ||
        !slot_active[slot_id - 1U] || !buffer || !actual_length || length == 0U ||
        length > XHCI_BULK_BUFFER_SIZE)
        return 0;
    *actual_length = 0U;
    uint32_t index = slot_id - 1U;
    uint32_t direction = (endpoint_address & 0x80U) != 0U ? 1U : 0U;
    uint32_t endpoint_number = endpoint_address & 0x0FU;
    if (endpoint_number == 0U || !bulk_configured[index][direction] ||
        bulk_dci[index][direction] != endpoint_number * 2U + direction)
        return 0;

    uint8_t* dma = bulk_dma[index][direction];
    if (!direction) {
        const uint8_t* source = (const uint8_t*)buffer;
        for (uint32_t i = 0; i < length; ++i)
            dma[i] = source[i];
    }
    uint32_t ring_index = bulk_enqueue[index][direction];
    if (ring_index >= XHCI_EP0_RING_TRBS - 1U)
        return 0;
    xhci_trb_t* trb = &bulk_rings[index][direction][ring_index];
    uint64_t data_physical, trb_physical;
    if (!dma_address(dma, &data_physical) ||
        !dma_address(trb, &trb_physical))
        return 0;
    if (bulk_in_flight[index][direction])
        return 0;
    bulk_done[index][direction] = 0U;
    bulk_completion_code[index][direction] = 0U;
    bulk_completion_status[index][direction] = 0U;
    trb->parameter = data_physical;
    trb->status = length;
    trb->control = (1U << XHCI_TRB_TYPE_SHIFT) | (1U << 5) |
                   (direction ? (1U << 2) : 0U) |
                   (bulk_cycle[index][direction] ? XHCI_TRB_CYCLE : 0U);
    bulk_enqueue[index][direction]++;
    if (bulk_enqueue[index][direction] == XHCI_EP0_RING_TRBS - 1U) {
        xhci_trb_t* link =
            &bulk_rings[index][direction][XHCI_EP0_RING_TRBS - 1U];
        uint64_t ring_physical;
        if (!dma_address(bulk_rings[index][direction], &ring_physical))
            return 0;
        link->parameter = ring_physical;
        link->status = 0;
        link->control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                        (1U << 1) |
                        (bulk_cycle[index][direction] ? XHCI_TRB_CYCLE : 0U);
        bulk_enqueue[index][direction] = 0;
        bulk_cycle[index][direction] ^= 1U;
    }
    bulk_expected_trb[index][direction] = trb_physical;
    bulk_in_flight[index][direction] = 1U;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    mmio_write32(doorbells, slot_id * 4U, bulk_dci[index][direction]);
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        consume_event_ring();
        if (bulk_done[index][direction]) {
            uint32_t completion_code = bulk_completion_code[index][direction];
            uint32_t completion_status = bulk_completion_status[index][direction];
            bulk_in_flight[index][direction] = 0U;
            bulk_done[index][direction] = 0U;
            if (completion_code != 1U)
                return 0;
            uint32_t residual = completion_status & 0xFFFFFFU;
            if (residual > length)
                return 0;
            uint32_t actual = length - residual;
            if (direction) {
                for (uint32_t j = 0; j < actual; ++j)
                    ((uint8_t*)buffer)[j] = dma[j];
            }
            *actual_length = actual;
            return 1;
        }
        if (mmio_read32(operational, XHCI_USBSTS) & XHCI_STS_HOST_ERROR) {
            bulk_in_flight[index][direction] = 0U;
            return 0;
        }
        __asm__ volatile ("pause");
    }
    bulk_in_flight[index][direction] = 0U;
    return 0;
}

int xhci_start_bulk_in(uint32_t slot_id, uint8_t endpoint_address)
{
    if (!controller_online || slot_id == 0U || slot_id > XHCI_MAX_SLOTS ||
        !slot_active[slot_id - 1U] || (endpoint_address & 0x80U) == 0U)
        return 0;
    uint32_t index = slot_id - 1U;
    uint32_t endpoint_number = endpoint_address & 0x0FU;
    uint32_t direction = 1U;
    if (endpoint_number == 0U || !bulk_configured[index][direction] ||
        bulk_dci[index][direction] != endpoint_number * 2U + 1U ||
        bulk_in_flight[index][direction])
        return 0;

    uint32_t ring_index = bulk_enqueue[index][direction];
    if (ring_index >= XHCI_EP0_RING_TRBS - 1U)
        return 0;
    xhci_trb_t *trb = &bulk_rings[index][direction][ring_index];
    uint64_t data_physical, trb_physical;
    if (!dma_address(bulk_dma[index][direction], &data_physical) ||
        !dma_address(trb, &trb_physical))
        return 0;

    trb->parameter = data_physical;
    trb->status = XHCI_BULK_BUFFER_SIZE;
    trb->control = (1U << XHCI_TRB_TYPE_SHIFT) | (1U << 5) |
                   (1U << 2) |
                   (bulk_cycle[index][direction] ? XHCI_TRB_CYCLE : 0U);
    bulk_enqueue[index][direction]++;
    if (bulk_enqueue[index][direction] == XHCI_EP0_RING_TRBS - 1U) {
        xhci_trb_t *link = &bulk_rings[index][direction][XHCI_EP0_RING_TRBS - 1U];
        uint64_t ring_physical;
        if (!dma_address(bulk_rings[index][direction], &ring_physical))
            return 0;
        link->parameter = ring_physical;
        link->status = 0;
        link->control = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                        (1U << 1) |
                        (bulk_cycle[index][direction] ? XHCI_TRB_CYCLE : 0U);
        bulk_enqueue[index][direction] = 0;
        bulk_cycle[index][direction] ^= 1U;
    }

    bulk_expected_trb[index][direction] = trb_physical;
    bulk_completion_code[index][direction] = 0U;
    bulk_completion_status[index][direction] = 0U;
    bulk_done[index][direction] = 0U;
    bulk_in_flight[index][direction] = 1U;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    mmio_write32(doorbells, slot_id * 4U, bulk_dci[index][direction]);
    return 1;
}

int xhci_take_bulk_in(uint32_t slot_id, uint8_t endpoint_address,
                      void *buffer, uint32_t capacity, uint32_t *actual_length)
{
    if (!controller_online || slot_id == 0U || slot_id > XHCI_MAX_SLOTS ||
        !buffer || !actual_length || (endpoint_address & 0x80U) == 0U)
        return -1;
    uint32_t index = slot_id - 1U;
    uint32_t endpoint_number = endpoint_address & 0x0FU;
    if (endpoint_number == 0U || !bulk_configured[index][1] ||
        bulk_dci[index][1] != endpoint_number * 2U + 1U)
        return -1;

    *actual_length = 0U;
    consume_event_ring();
    if (!bulk_done[index][1])
        return 0;

    uint32_t completion_code = bulk_completion_code[index][1];
    uint32_t completion_status = bulk_completion_status[index][1];
    bulk_done[index][1] = 0U;
    bulk_in_flight[index][1] = 0U;
    if (completion_code != 1U && completion_code != 13U)
        return -1;

    uint32_t residual = completion_status & 0xFFFFFFU;
    if (residual > XHCI_BULK_BUFFER_SIZE)
        return -1;
    uint32_t actual = XHCI_BULK_BUFFER_SIZE - residual;
    if (actual > capacity)
        return -1;
    uint8_t *source = bulk_dma[index][1];
    uint8_t *destination = buffer;
    for (uint32_t i = 0; i < actual; ++i)
        destination[i] = source[i];
    *actual_length = actual;
    return 1;
}

int xhci_start_interrupt_in(uint32_t slot_id)
{
    if (!controller_online || slot_id == 0U || slot_id > XHCI_MAX_SLOTS ||
        !interrupt_configured[slot_id - 1U])
        return 0;
    return submit_interrupt_trb(slot_id - 1U);
}

void xhci_poll(void)
{
    if (controller_online)
        consume_event_ring();
}

int xhci_take_interrupt_report(uint32_t slot_id, uint8_t* output,
                               uint32_t capacity, uint32_t* length_out)
{
    if (!controller_online || slot_id == 0U || slot_id > XHCI_MAX_SLOTS ||
        !output || !length_out)
        return 0;
    consume_event_ring();
    uint32_t index = slot_id - 1U;
    if (!interrupt_configured[index] || !interrupt_report_ready[index]) {
        if (interrupt_configured[index] && !interrupt_in_flight[index])
            (void)submit_interrupt_trb(index);
        return 0;
    }
    uint32_t length = interrupt_pending_length[index];
    if (length > capacity)
        length = capacity;
    for (uint32_t i = 0; i < length; ++i)
        output[i] = interrupt_pending[index][i];
    *length_out = length;
    interrupt_report_ready[index] = 0;
    interrupt_pending_length[index] = 0;
    (void)submit_interrupt_trb(index);
    return 1;
}
uint32_t xhci_port_speed(uint32_t port)
{
    return (xhci_port_status(port) >> 10) & 0xFU;
}
uint32_t xhci_port_count(void)
{
    return controller_online ? max_ports : 0U;
}

uint32_t xhci_port_status(uint32_t port)
{
    if (!controller_online || port == 0U || port > max_ports)
        return 0;
    return mmio_read32(operational,
                       XHCI_PORTSC_BASE + (port - 1U) * 0x10U);
}

void xhci_ack_port_changes(uint32_t port)
{
    if (!controller_online || port == 0U || port > max_ports)
        return;
    uint32_t offset = XHCI_PORTSC_BASE + (port - 1U) * 0x10U;
    uint32_t portsc = mmio_read32(operational, offset);
    uint32_t changes = portsc & XHCI_PORT_CHANGE_BITS;
    if (changes != 0U) {
        /* Preserve writable power/indicator/link-write controls, but do not
           echo the RW1C port-enable or reset bits. */
        uint32_t controls = portsc & (XHCI_PORT_POWER | (3U << 14));
        mmio_write32(operational, offset, controls | changes);
    }
}
int xhci_reset_port(uint32_t port)
{
    if (!controller_online || port == 0U || port > max_ports)
        return 0;

    uint32_t offset = XHCI_PORTSC_BASE + (port - 1U) * 0x10U;
    uint32_t portsc = mmio_read32(operational, offset);
    if ((portsc & XHCI_PORT_CONNECTED) == 0U)
        return 0;

    uint32_t writable = portsc & ~(XHCI_PORT_CHANGE_BITS |
                                   XHCI_PORT_ENABLED | (1U << 16) |
                                   XHCI_PORT_RESET |
                                   XHCI_PORT_WARM_RESET);
    writable |= XHCI_PORT_POWER;
    mmio_write32(operational, offset, writable);
    portsc = mmio_read32(operational, offset);
    uint32_t speed = (portsc >> 10) & 0x0FU;
    uint32_t reset_bit = speed >= 4U
        ? XHCI_PORT_WARM_RESET : XHCI_PORT_RESET;
    writable = portsc & ~(XHCI_PORT_CHANGE_BITS |
                          XHCI_PORT_ENABLED | (1U << 16) |
                          XHCI_PORT_RESET |
                          XHCI_PORT_WARM_RESET);
    writable |= reset_bit;
    mmio_write32(operational, offset, writable);
    if (!wait_register(operational, offset, reset_bit, 0))
        return 0;
    portsc = mmio_read32(operational, offset);
    return (portsc & XHCI_PORT_ENABLED) != 0U;
}
static int controller_start(const struct pci_device* pci)
{
    uint64_t bar = 0;
    int is_io = 0, is_64bit = 0;
    if (!pci_get_bar(pci, 0, &bar, &is_io, &is_64bit) || is_io || bar == 0)
        return 0;
    if (!pci_enable_memory_space(pci) || !map_controller_bar(bar))
        return 0;

    uint8_t cap_length = registers[XHCI_CAP_CAPLENGTH];
    uint32_t hcs1 = mmio_read32(registers, XHCI_CAP_HCSPARAMS1);
    uint32_t hcs2 = mmio_read32(registers, XHCI_CAP_HCSPARAMS2);
    uint32_t hcc1 = mmio_read32(registers, XHCI_CAP_HCCPARAMS1);
    uint32_t doorbell_offset = mmio_read32(registers, XHCI_CAP_DBOFF) & ~3U;
    uint32_t runtime_offset = mmio_read32(registers, XHCI_CAP_RTSOFF) & ~0x1FU;

    if (cap_length < 0x20U || doorbell_offset >= XHCI_MMIO_MAP_SIZE ||
        runtime_offset + 0x40U >= XHCI_MMIO_MAP_SIZE)
        return 0;

    operational = registers + cap_length;
    doorbells = registers + doorbell_offset;
    runtime = registers + runtime_offset;
    context_size = (hcc1 & (1U << 2)) ? 64U : 32U;
    max_slots = hcs1 & 0xFFU;
    max_ports = (hcs1 >> 24) & 0xFFU;
    if (max_slots == 0 || max_ports == 0)
        return 0;
    if (max_ports > XHCI_MAX_PORTS)
        max_ports = XHCI_MAX_PORTS;

    /* Stop the controller before resetting it or changing ring pointers. */
    uint32_t command = mmio_read32(operational, XHCI_USBCMD);
    command &= ~(XHCI_CMD_RUN | XHCI_CMD_INTERRUPTS);
    mmio_write32(operational, XHCI_USBCMD, command);
    if (!wait_register(operational, XHCI_USBSTS, XHCI_STS_HALTED,
                       XHCI_STS_HALTED))
        return 0;

    mmio_write32(operational, XHCI_USBCMD,
                 mmio_read32(operational, XHCI_USBCMD) | XHCI_CMD_RESET);
    if (!wait_register(operational, XHCI_USBCMD, XHCI_CMD_RESET, 0))
        return 0;
    if (!wait_register(operational, XHCI_USBSTS, XHCI_STS_NOT_READY, 0))
        return 0;

    /* Clear static DMA state after reset; all pointers remain below 4 GiB. */
    for (uint32_t i = 0; i < 256U; ++i)
        dcbaa[i] = 0;
    for (uint32_t i = 0; i < XHCI_RING_TRBS; ++i) {
        command_ring[i].parameter = 0;
        command_ring[i].status = 0;
        command_ring[i].control = 0;
        event_ring[i].parameter = 0;
        event_ring[i].status = 0;
        event_ring[i].control = 0;
    }
    command_enqueue = 0;
    command_cycle = 1;
    event_dequeue = 0;
    event_cycle = 1;
    port_event_mask = 0;
    command_completion_sequence = 0;
    transfer_completion_sequence = 0;
    for (uint32_t slot = 0; slot < XHCI_MAX_SLOTS; ++slot) {
        slot_active[slot] = 0;
        slot_port[slot] = 0;
        slot_speed[slot] = 0;
        slot_ep0_packet[slot] = 0;
        ep0_enqueue[slot] = 0;
        ep0_cycle[slot] = 1;
        interrupt_enqueue[slot] = 0;
        interrupt_cycle[slot] = 1;
        interrupt_dci[slot] = 0;
        interrupt_packet[slot] = 0;
        interrupt_pending_length[slot] = 0;
        interrupt_configured[slot] = 0;
        interrupt_in_flight[slot] = 0;
        interrupt_report_ready[slot] = 0;
        interrupt_endpoint_address[slot] = 0;
        for (uint32_t direction = 0; direction < 2U; ++direction) {
            bulk_enqueue[slot][direction] = 0;
            bulk_cycle[slot][direction] = 1;
            bulk_in_flight[slot][direction] = 0;
            bulk_done[slot][direction] = 0;
            bulk_expected_trb[slot][direction] = 0;
            bulk_completion_code[slot][direction] = 0;
            bulk_completion_status[slot][direction] = 0;
            bulk_dci[slot][direction] = 0;
            bulk_packet[slot][direction] = 0;
            bulk_configured[slot][direction] = 0;
        }
    }

    uint32_t scratchpads =
        (((hcs2 >> 27) & 0x1FU) << 5) | ((hcs2 >> 21) & 0x1FU);
    if (scratchpads > XHCI_SCRATCHPAD_MAX)
        return 0;
    if (scratchpads != 0U) {
        for (uint32_t i = 0; i < scratchpads; ++i) {
            uint64_t address;
            if (!dma_address(scratchpad_buffers[i], &address))
                return 0;
            scratchpad_array[i] = address;
        }
        uint64_t array_address;
        if (!dma_address(scratchpad_array, &array_address))
            return 0;
        dcbaa[0] = array_address;
    }

    uint64_t dcbaa_address, command_address, event_address, erst_address;
    if (!dma_address(dcbaa, &dcbaa_address) ||
        !dma_address(command_ring, &command_address) ||
        !dma_address(event_ring, &event_address) ||
        !dma_address(erst, &erst_address))
        return 0;

    command_ring[XHCI_RING_TRBS - 1U].parameter = command_address;
    command_ring[XHCI_RING_TRBS - 1U].status = 0;
    command_ring[XHCI_RING_TRBS - 1U].control =
        (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
        (1U << 1) | XHCI_TRB_CYCLE;

    erst[0].address = event_address;
    erst[0].size = XHCI_RING_TRBS;
    erst[0].reserved = 0;

    mmio_write64(operational, XHCI_DCBAAP, dcbaa_address);
    mmio_write64(operational, XHCI_CRCR, command_address | 1U);
    if (max_slots > XHCI_MAX_SLOTS)
        max_slots = XHCI_MAX_SLOTS;
    mmio_write32(operational, XHCI_CONFIG, max_slots);

    /* Polling mode: keep interrupter zero masked and publish one ERST segment. */
    mmio_write32(runtime, 0x20U, 0);
    mmio_write32(runtime, 0x28U, 1);
    mmio_write64(runtime, 0x30U, erst_address);
    mmio_write64(runtime, 0x38U, event_address);

    if (!pci_enable_bus_mastering(pci))
        return 0;

    command = mmio_read32(operational, XHCI_USBCMD);
    command &= ~XHCI_CMD_INTERRUPTS;
    command |= XHCI_CMD_RUN;
    mmio_write32(operational, XHCI_USBCMD, command);
    if (!wait_register(operational, XHCI_USBSTS, XHCI_STS_HALTED, 0))
        return 0;
    if (mmio_read32(operational, XHCI_USBSTS) & XHCI_STS_HOST_ERROR)
        return 0;

    /* Power and reset connected ports; device enumeration follows in the
       USB core and consumes port-status-change events from this ring. */
    for (uint32_t port = 1; port <= max_ports; ++port) {
        uint32_t offset = XHCI_PORTSC_BASE + (port - 1U) * 0x10U;
        uint32_t portsc = mmio_read32(operational, offset);
        if ((portsc & XHCI_PORT_CONNECTED) == 0U)
            continue;

        uint32_t writable = portsc & ~(XHCI_PORT_CHANGE_BITS | XHCI_PORT_ENABLED | (1U << 16) |
                                      XHCI_PORT_RESET |
                                      XHCI_PORT_WARM_RESET);
        writable |= XHCI_PORT_POWER;
        mmio_write32(operational, offset, writable);
        portsc = mmio_read32(operational, offset);
        if ((portsc & XHCI_PORT_CONNECTED) != 0U) {
            writable = portsc & ~(XHCI_PORT_CHANGE_BITS | XHCI_PORT_ENABLED | (1U << 16) |
                                  XHCI_PORT_RESET | XHCI_PORT_WARM_RESET);
            uint32_t speed = (portsc >> 10) & 0x0FU;
            uint32_t reset_bit = speed >= 4U
                ? XHCI_PORT_WARM_RESET : XHCI_PORT_RESET;
            writable |= reset_bit;
            mmio_write32(operational, offset, writable);
            (void)wait_register(operational, offset, reset_bit, 0);
        }
    }

    /* Consume any initial events before reporting the host as online. */
    consume_event_ring();
    (void)hcc1;
    (void)is_64bit;
    return 1;
}

void xhci_initialize(void)
{
    if (initialized)
        return;
    initialized = 1;

    int index = -1;
    while ((index = pci_find_class(XHCI_PCI_CLASS, XHCI_PCI_SUBCLASS,
                                   XHCI_PCI_PROG_IF, index + 1)) >= 0) {
        const struct pci_device* pci = pci_get_device(index);
        if (!pci)
            continue;
        if (controller_start(pci)) {
            controller_online = 1;
            serial_write("VANTAOS_XHCI_CONTROLLER_ONLINE\n");
            return;
        }
        serial_write("VANTAOS_XHCI_CONTROLLER_INIT_FAILED\n");
    }

    serial_write("VANTAOS_XHCI_CONTROLLER_NOT_FOUND\n");
}

int xhci_controller_online(void)
{
    return controller_online;
}

unsigned int xhci_take_port_events(void)
{
    consume_event_ring();
    uint32_t events = port_event_mask;
    port_event_mask = 0;
    return events;
}
