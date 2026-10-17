#include "xhci.h"

#include "paging.h"
#include "pci.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define XHCI_PCI_CLASS       0x0CU
#define XHCI_PCI_SUBCLASS    0x03U
#define XHCI_PCI_PROG_IF     0x30U

#define XHCI_MMIO_BASE       0xE0000000ULL
#define XHCI_MMIO_MAP_SIZE   0x00040000ULL
#define XHCI_MAX_PORTS       32U
#define XHCI_RING_TRBS       256U
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
                               (1U << 20) | (1U << 21) | (1U << 22))

#define XHCI_TRB_CYCLE        (1U << 0)
#define XHCI_TRB_TYPE_SHIFT   10U
#define XHCI_TRB_TYPE_LINK    6U
#define XHCI_TRB_TYPE_EVENT_COMMAND_COMPLETION 33U
#define XHCI_TRB_TYPE_EVENT_PORT_STATUS       34U
#define XHCI_TRB_TYPE_CMD_ENABLE_SLOT         9U
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

static uint64_t mmio_read64(volatile uint8_t* base, uint32_t offset)
{
    uint32_t low = mmio_read32(base, offset);
    uint32_t high = mmio_read32(base, offset + 4U);
    return ((uint64_t)high << 32) | low;
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
                   (control_flags & 0x3FFU) |
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

int xhci_reset_port(uint32_t port)
{
    if (!controller_online || port == 0U || port > max_ports)
        return 0;

    uint32_t offset = XHCI_PORTSC_BASE + (port - 1U) * 0x10U;
    uint32_t portsc = mmio_read32(operational, offset);
    if ((portsc & XHCI_PORT_CONNECTED) == 0U)
        return 0;

    uint32_t writable = portsc & ~(XHCI_PORT_CHANGE_BITS |
                                   XHCI_PORT_ENABLED |
                                   XHCI_PORT_RESET |
                                   XHCI_PORT_WARM_RESET);
    writable |= XHCI_PORT_POWER;
    mmio_write32(operational, offset, writable);
    portsc = mmio_read32(operational, offset);
    writable = portsc & ~(XHCI_PORT_CHANGE_BITS |
                          XHCI_PORT_ENABLED |
                          XHCI_PORT_WARM_RESET);
    writable |= XHCI_PORT_RESET;
    mmio_write32(operational, offset, writable);
    if (!wait_register(operational, offset, XHCI_PORT_RESET, 0))
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

        uint32_t writable = portsc & ~(XHCI_PORT_CHANGE_BITS | XHCI_PORT_ENABLED |
                                      XHCI_PORT_RESET |
                                      XHCI_PORT_WARM_RESET);
        writable |= XHCI_PORT_POWER;
        mmio_write32(operational, offset, writable);
        portsc = mmio_read32(operational, offset);
        if ((portsc & XHCI_PORT_CONNECTED) != 0U) {
            writable = portsc & ~(XHCI_PORT_CHANGE_BITS | XHCI_PORT_ENABLED |
                                  XHCI_PORT_WARM_RESET);
            writable |= XHCI_PORT_RESET;
            mmio_write32(operational, offset, writable);
            (void)wait_register(operational, offset, XHCI_PORT_RESET, 0);
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
