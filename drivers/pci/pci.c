/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "pci.h"
#include "ktrace.h"
#include "acpi.h"
#include "mmio.h"
#include "spinlock.h"
#include "vanta_boot_info.h"
#include "serial.h"

#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_INVALID_VENDOR 0xFFFF

#define PCI_MAX_DEVICES 1024

static struct pci_device devices[PCI_MAX_DEVICES];
static int pci_device_count = 0;
static spinlock_t pci_config_lock = SPINLOCK_INITIALIZER;
#ifndef PCI_TEST
static acpi_mcfg_region_t pci_mcfg_regions[ACPI_MAX_MCFG_REGIONS];
static uint32_t pci_mcfg_region_count;
static int pci_mcfg_initialized;
/* A single 1 MiB bus window is held while enumerating that ECAM bus. */
static void* pci_scan_ecam_mapping;
static uint16_t pci_scan_ecam_segment;
static uint8_t pci_scan_ecam_bus;
#endif

#ifdef PCI_TEST
/* Host-side tests replace privileged port I/O with a deterministic config model. */
void pci_test_outl(uint16_t port, uint32_t value);
uint32_t pci_test_inl(uint16_t port);
int pci_test_ecam_read32(uint16_t segment, uint8_t bus, uint8_t slot,
                         uint8_t function, uint16_t offset,
                         uint32_t* value);
int pci_test_ecam_write32(uint16_t segment, uint8_t bus, uint8_t slot,
                          uint8_t function, uint16_t offset,
                          uint32_t value);

static inline void outl(uint16_t port, uint32_t value)
{
    pci_test_outl(port, value);
}

static inline uint32_t inl(uint16_t port)
{
    return pci_test_inl(port);
}

static inline void outw(uint16_t port, uint16_t value)
{
    uint16_t aligned_port = (uint16_t)(port & (uint16_t)~3U);
    uint32_t old_value = pci_test_inl(aligned_port);
    unsigned shift = (unsigned)(port & 2U) * 8U;
    uint32_t mask = 0xFFFFU << shift;
    pci_test_outl(aligned_port, (old_value & ~mask) |
                  ((uint32_t)value << shift));
}
#else
static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile (
        "inl %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );
    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}
#endif

static uint64_t pci_lock_config(void)
{
#ifdef PCI_TEST
    spinlock_lock(&pci_config_lock);
    return 0;
#else
    return spinlock_lock_irqsave(&pci_config_lock);
#endif
}

static void pci_unlock_config(uint64_t flags)
{
#ifdef PCI_TEST
    (void)flags;
    spinlock_unlock(&pci_config_lock);
#else
    spinlock_unlock_irqrestore(&pci_config_lock, flags);
#endif
}

static uint32_t pci_config_address(
    uint8_t bus,
    uint8_t slot,
    uint8_t function,
    uint8_t offset
)
{
    return
        (1U << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);
}

#ifndef PCI_TEST
static const acpi_mcfg_region_t* pci_find_mcfg_region(
    uint16_t segment, uint8_t bus)
{
    for (uint32_t i = 0U; i < pci_mcfg_region_count; ++i)
    {
        const acpi_mcfg_region_t* region = &pci_mcfg_regions[i];
        if (region->segment_group == segment &&
            bus >= region->start_bus && bus <= region->end_bus)
            return region;
    }
    return NULL;
}

static volatile uint32_t* pci_ecam_dword(
    const struct pci_device* device, uint16_t offset)
{
    const acpi_mcfg_region_t* region =
        pci_find_mcfg_region(device->segment_group, device->bus);
    uint64_t bus_offset;
    uint64_t physical;

    if (!region || offset > 0xFFCU || (offset & 3U) != 0U)
        return NULL;

    bus_offset =
        ((uint64_t)(device->bus - region->start_bus) << 20U) |
        ((uint64_t)device->slot << 15U) |
        ((uint64_t)device->function << 12U);
    if (region->base_address > UINT64_MAX - bus_offset)
        return NULL;
    physical = region->base_address + bus_offset;
    if (physical > UINT64_MAX - offset)
        return NULL;
    physical += offset;
    return (volatile uint32_t*)mmio_map_region(physical, sizeof(uint32_t));
}
#endif


/* Legacy configuration helpers are also used when no ECAM range exists. */
static uint32_t pci_read_config_locked(
    uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
static void pci_write_config_locked(
    uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset,
    uint32_t value);

/* Caller holds pci_config_lock. */
static uint32_t pci_read_device_config_locked(
    const struct pci_device* device, uint8_t offset)
{
#ifndef PCI_TEST
    if (device->segment_group != 0U ||
        pci_find_mcfg_region(device->segment_group, device->bus) != NULL)
    {
        if (pci_scan_ecam_mapping &&
            pci_scan_ecam_segment == device->segment_group &&
            pci_scan_ecam_bus == device->bus)
        {
            uintptr_t entry = (uintptr_t)pci_scan_ecam_mapping +
                ((uintptr_t)device->slot << 15U) +
                ((uintptr_t)device->function << 12U) + offset;
            return *(volatile uint32_t*)entry;
        }
        volatile uint32_t* mapped = pci_ecam_dword(device, offset);
        uint32_t value;
        if (!mapped)
            return 0xFFFFFFFFU;
        value = *mapped;
        (void)mmio_unmap_region((void*)mapped, sizeof(uint32_t));
        return value;
    }
#else
    if (device->segment_group != 0U)
    {
        uint32_t value = 0xFFFFFFFFU;
        if (!pci_test_ecam_read32(device->segment_group, device->bus,
                                  device->slot, device->function,
                                  offset, &value))
            return 0xFFFFFFFFU;
        return value;
    }
#endif
    return pci_read_config_locked(device->bus, device->slot,
                                  device->function, offset);
}

/* Caller holds pci_config_lock. */
static int pci_write_device_config_locked(
    const struct pci_device* device, uint8_t offset, uint32_t value)
{
#ifndef PCI_TEST
    if (device->segment_group != 0U ||
        pci_find_mcfg_region(device->segment_group, device->bus) != NULL)
    {
        if (pci_scan_ecam_mapping &&
            pci_scan_ecam_segment == device->segment_group &&
            pci_scan_ecam_bus == device->bus)
        {
            uintptr_t entry = (uintptr_t)pci_scan_ecam_mapping +
                ((uintptr_t)device->slot << 15U) +
                ((uintptr_t)device->function << 12U) + offset;
            *(volatile uint32_t*)entry = value;
            return 1;
        }
        volatile uint32_t* mapped = pci_ecam_dword(device, offset);
        if (!mapped)
            return 0;
        *mapped = value;
        (void)mmio_unmap_region((void*)mapped, sizeof(uint32_t));
        return 1;
    }
#else
    if (device->segment_group != 0U)
        return pci_test_ecam_write32(device->segment_group, device->bus,
                                     device->slot, device->function,
                                     offset, value);
#endif
    pci_write_config_locked(device->bus, device->slot,
                            device->function, offset, value);
    return 1;
}

static int pci_write_device_config16_locked(
    const struct pci_device* device, uint8_t offset, uint16_t value)
{
#ifndef PCI_TEST
    if (device->segment_group != 0U ||
        pci_find_mcfg_region(device->segment_group, device->bus) != NULL)
    {
        if (pci_scan_ecam_mapping &&
            pci_scan_ecam_segment == device->segment_group &&
            pci_scan_ecam_bus == device->bus)
        {
            uintptr_t entry = (uintptr_t)pci_scan_ecam_mapping +
                ((uintptr_t)device->slot << 15U) +
                ((uintptr_t)device->function << 12U) + offset;
            *(volatile uint16_t*)entry = value;
            return 1;
        }
        volatile uint32_t* mapped =
            pci_ecam_dword(device, (uint8_t)(offset & 0xFCU));
        if (!mapped)
            return 0;
        *(volatile uint16_t*)((uintptr_t)mapped + (offset & 2U)) = value;
        (void)mmio_unmap_region((void*)mapped, sizeof(uint32_t));
        return 1;
    }
#else
    if (device->segment_group != 0U)
    {
        uint32_t current = 0xFFFFFFFFU;
        uint32_t shift = (uint32_t)(offset & 2U) * 8U;
        if (!pci_test_ecam_read32(device->segment_group, device->bus,
                                  device->slot, device->function,
                                  (uint16_t)(offset & 0xFCU), &current))
            return 0;
        current = (current & ~(0xFFFFU << shift)) |
                  ((uint32_t)value << shift);
        return pci_test_ecam_write32(device->segment_group, device->bus,
                                     device->slot, device->function,
                                     (uint16_t)(offset & 0xFCU), current);
    }
#endif
    outl(PCI_CONFIG_ADDRESS,
         pci_config_address(device->bus, device->slot,
                            device->function, offset));
    outw((uint16_t)(PCI_CONFIG_DATA + (offset & 2U)), value);
    return 1;
}

static uint32_t pci_read_device_config(
    const struct pci_device* device, uint8_t offset)
{
    uint64_t flags;
    uint32_t value;
    if (!device)
        return 0xFFFFFFFFU;
    flags = pci_lock_config();
    value = pci_read_device_config_locked(device, offset);
    pci_unlock_config(flags);
    return value;
}

/* These helpers require pci_config_lock to be held by the caller. */
static uint32_t pci_read_config_locked(
    uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA);
}

static void pci_write_config_locked(
    uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset,
    uint32_t value)
{
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

static void pci_decode_bridge_windows(struct pci_device* device)
{
    uint32_t io_register = pci_read_device_config(device, 0x1CU);
    uint32_t memory_register = pci_read_device_config(device, 0x20U);
    uint32_t prefetch_register = pci_read_device_config(device, 0x24U);
    uint32_t prefetch_base_upper = pci_read_device_config(device, 0x28U);
    uint32_t prefetch_limit_upper = pci_read_device_config(device, 0x2CU);
    uint32_t io_upper = pci_read_device_config(device, 0x30U);
    uint8_t io_base_type = (uint8_t)(io_register & 0x0FU);
    uint8_t io_limit_type = (uint8_t)((io_register >> 8U) & 0x0FU);
    uint8_t prefetch_base_type = (uint8_t)(prefetch_register & 0x0FU);
    uint8_t prefetch_limit_type =
        (uint8_t)((prefetch_register >> 16U) & 0x0FU);

    device->io_window_base = 0U;
    device->io_window_limit = 0U;
    device->memory_window_base = 0U;
    device->memory_window_limit = 0U;
    device->prefetch_window_base = 0U;
    device->prefetch_window_limit = 0U;
    device->io_window_valid = 0U;
    device->memory_window_valid = 0U;
    device->prefetch_window_valid = 0U;

    /* I/O windows use 4 KiB granularity and may be 16- or 32-bit. */
    if (io_base_type == io_limit_type &&
        (io_base_type == 0U || io_base_type == 1U))
    {
        uint64_t base = (uint64_t)(io_register & 0x00F0U) << 8U;
        uint64_t limit =
            ((uint64_t)((io_register >> 8U) & 0x00F0U) << 8U) |
            0x0FFFU;
        if (io_base_type == 1U)
        {
            base |= (uint64_t)(io_upper & 0x0000FFFFU) << 16U;
            limit |= (uint64_t)(io_upper >> 16U) << 16U;
        }
        if (base <= limit)
        {
            device->io_window_base = base;
            device->io_window_limit = limit;
            device->io_window_valid = 1U;
        }
    }

    /* Non-prefetchable memory windows have fixed 1 MiB granularity. */
    {
        uint64_t base = (uint64_t)(memory_register & 0x0000FFF0U) << 16U;
        uint64_t limit =
            ((uint64_t)((memory_register >> 16U) & 0x0000FFF0U) << 16U) |
            0x000FFFFFULL;
        if (base <= limit)
        {
            device->memory_window_base = base;
            device->memory_window_limit = limit;
            device->memory_window_valid = 1U;
        }
    }

    /* Prefetchable windows may carry a 64-bit upper address. */
    if (prefetch_base_type == prefetch_limit_type &&
        (prefetch_base_type == 0U || prefetch_base_type == 1U))
    {
        uint64_t base =
            (uint64_t)(prefetch_register & 0x0000FFF0U) << 16U;
        uint64_t limit =
            ((uint64_t)((prefetch_register >> 16U) & 0x0000FFF0U) << 16U) |
            0x000FFFFFULL;
        if (prefetch_base_type == 1U)
        {
            base |= (uint64_t)prefetch_base_upper << 32U;
            limit |= (uint64_t)prefetch_limit_upper << 32U;
        }
        if (base <= limit)
        {
            device->prefetch_window_base = base;
            device->prefetch_window_limit = limit;
            device->prefetch_window_valid = 1U;
        }
    }
}

static void pci_read_device(struct pci_device* device,
                             uint16_t segment, uint8_t bus,
                             uint8_t slot, uint8_t function)
{
    uint32_t id;
    uint32_t class_info;
    uint32_t header_info;
    uint32_t interrupt_info;

    device->bus = bus;
    device->slot = slot;
    device->function = function;
    device->segment_group = segment;

    id = pci_read_device_config(device, 0x00U);
    class_info = pci_read_device_config(device, 0x08U);
    header_info = pci_read_device_config(device, 0x0CU);
    interrupt_info = pci_read_device_config(device, 0x3CU);
    device->vendor_id = (uint16_t)(id & 0xFFFFU);
    device->device_id = (uint16_t)(id >> 16U);
    device->revision = (uint8_t)(class_info & 0xFFU);
    device->prog_if = (uint8_t)((class_info >> 8U) & 0xFFU);
    device->subclass = (uint8_t)((class_info >> 16U) & 0xFFU);
    device->class_code = (uint8_t)((class_info >> 24U) & 0xFFU);
    device->header_type = (uint8_t)((header_info >> 16U) & 0x7FU);
    device->interrupt_line = (uint8_t)(interrupt_info & 0xFFU);
    device->interrupt_pin = (uint8_t)((interrupt_info >> 8U) & 0xFFU);

    device->primary_bus = 0U;
    device->secondary_bus = 0U;
    device->subordinate_bus = 0U;
    if (device->header_type == 1U)
    {
        uint32_t bus_numbers = pci_read_device_config(device, 0x18U);
        device->primary_bus = (uint8_t)(bus_numbers & 0xFFU);
        device->secondary_bus = (uint8_t)((bus_numbers >> 8U) & 0xFFU);
        device->subordinate_bus = (uint8_t)((bus_numbers >> 16U) & 0xFFU);
        pci_decode_bridge_windows(device);
    }
}

static void pci_scan_range(uint16_t segment, uint8_t first_bus,
                           uint8_t last_bus)
{
    int capacity_reached = 0;
    for (uint16_t bus = first_bus; bus <= last_bus; ++bus)
    {
#ifndef PCI_TEST
        const acpi_mcfg_region_t* region =
            pci_find_mcfg_region(segment, (uint8_t)bus);
        if (region)
        {
            uint64_t bus_offset =
                (uint64_t)((uint8_t)bus - region->start_bus) << 20U;
            if (region->base_address <= UINT64_MAX - bus_offset)
            {
                uint64_t physical = region->base_address + bus_offset;
                pci_scan_ecam_mapping = mmio_map_region(
                    physical, 1U << 20U);
                if (pci_scan_ecam_mapping)
                {
                    pci_scan_ecam_segment = segment;
                    pci_scan_ecam_bus = (uint8_t)bus;
                }
            }
        }
#endif
        for (uint8_t slot = 0U; slot < 32U; ++slot)
        {
            struct pci_device probe = {
                .bus = (uint8_t)bus, .slot = slot, .function = 0U,
                .segment_group = segment
            };
            uint32_t id = pci_read_device_config(&probe, 0x00U);
            if ((uint16_t)id == PCI_INVALID_VENDOR)
                continue;

            uint8_t header_type = (uint8_t)(
                pci_read_device_config(&probe, 0x0CU) >> 16U);
            uint8_t function_count =
                (header_type & 0x80U) != 0U ? 8U : 1U;

            for (uint8_t function = 0U; function < function_count; ++function)
            {
                probe.function = function;
                id = pci_read_device_config(&probe, 0x00U);
                if ((uint16_t)id == PCI_INVALID_VENDOR)
                    continue;
                if (pci_device_count >= PCI_MAX_DEVICES)
                {
                    capacity_reached = 1;
                    goto bus_complete;
                }

                pci_read_device(&devices[pci_device_count], segment,
                                (uint8_t)bus, slot, function);
                ++pci_device_count;
            }
        }
bus_complete:
#ifndef PCI_TEST
        if (pci_scan_ecam_mapping &&
            pci_scan_ecam_segment == segment &&
            pci_scan_ecam_bus == (uint8_t)bus)
        {
            (void)mmio_unmap_region(pci_scan_ecam_mapping, 1U << 20U);
            pci_scan_ecam_mapping = NULL;
        }
#endif
        if (capacity_reached)
            return;
    }
}

static void pci_scan(void)
{
    pci_device_count = 0U;
#ifndef PCI_TEST
    /* Segment zero remains discoverable through legacy CF8/CFC when firmware
       does not publish MCFG; published MCFG ranges use ECAM instead. */
    pci_scan_range(0U, 0U, 255U);
    for (uint32_t i = 0U; i < pci_mcfg_region_count; ++i)
    {
        const acpi_mcfg_region_t* region = &pci_mcfg_regions[i];
        if (region->segment_group == 0U)
            continue; /* Already covered, including ECAM-backed bus ranges. */
        pci_scan_range(region->segment_group,
                       region->start_bus, region->end_bus);
    }
#else
    pci_scan_range(0U, 0U, 255U);
#endif
}

static int pci_devices_registered;

static void pci_write_hex(char* output, uint32_t value, uint32_t digits)
{
    static const char hex[] = "0123456789abcdef";
    for (uint32_t i = 0U; i < digits; ++i)
    {
        uint32_t shift = (digits - i - 1U) * 4U;
        output[i] = hex[(value >> shift) & 0x0FU];
    }
}

static void pci_prepare_core_device(struct pci_device* pci,
                                    const struct pci_device* parent)
{
    char* name = pci->core.name;
    name[0] = 'p';
    name[1] = 'c';
    name[2] = 'i';
    pci_write_hex(name + 3U, pci->segment_group, 4U);
    name[7] = ':';
    pci_write_hex(name + 8U, pci->bus, 2U);
    name[10] = ':';
    pci_write_hex(name + 11U, pci->slot, 2U);
    name[13] = '.';
    pci_write_hex(name + 14U, pci->function, 1U);
    name[15] = '\0';

    pci->core.bus = DEVICE_BUS_PCI;
    pci->core.parent = parent ? (device_t*)&parent->core : NULL;
    pci->core.vendor_id = pci->vendor_id;
    pci->core.device_id = pci->device_id;
    pci->core.class_code = pci->class_code;
    pci->core.subclass = pci->subclass;
    pci->core.prog_if = pci->prog_if;
    pci->core.revision = pci->revision;
    pci->core.bus_data = pci;
    /* The PCI enumerator owns this node; generic callers cannot remove it. */
    pci->core.owner = pci;
    pci->core.driver = NULL;
    pci->core.state = DEVICE_STATE_UNREGISTERED;
}

static void pci_register_core_devices(void)
{
    if (pci_devices_registered)
        return;

    /*
     * Register roots before descendants. ACPI MCFG regions are not required
     * to be ordered by bus number, so use bounded passes rather than assuming
     * enumeration-array order is a valid parent-before-child order.
     */
    for (int pass = 0; pass < pci_device_count; ++pass)
    {
        int progress = 0;
        for (int i = 0; i < pci_device_count; ++i)
        {
            struct pci_device* pci = &devices[i];
            const struct pci_device* parent;
            if (pci->core.state != DEVICE_STATE_UNREGISTERED)
                continue;
            parent = pci_get_parent_bridge(pci);
            if (parent && parent->core.state == DEVICE_STATE_UNREGISTERED)
                continue;
            pci_prepare_core_device(pci, parent);
            if (device_register(&pci->core))
                progress = 1;
        }
        if (!progress)
            break;
    }

    /* Preserve discovery even when firmware reports an unusable bridge chain. */
    for (int i = 0; i < pci_device_count; ++i)
    {
        struct pci_device* pci = &devices[i];
        if (pci->core.state == DEVICE_STATE_UNREGISTERED)
        {
            pci_prepare_core_device(pci, NULL);
            (void)device_register(&pci->core);
        }
    }
    pci_devices_registered = 1;
}

void pci_initialize(void)
{
    uint64_t mcfg_regions = 0U;
    if (pci_devices_registered)
        return;
#ifndef PCI_TEST
    pci_mcfg_region_count = acpi_discover_pci_mcfg(
        pci_mcfg_regions, ACPI_MAX_MCFG_REGIONS);
    pci_mcfg_initialized = 1;
    mcfg_regions = pci_mcfg_region_count;
#endif
    pci_scan();
    pci_register_core_devices();
    ktrace_emit(KTRACE_EVENT_PCI_SCAN_COMPLETE, KTRACE_CPU_UNKNOWN,
                (uint64_t)pci_device_count, mcfg_regions);
}

int pci_get_device_count(void)
{
    return pci_device_count;
}

const struct pci_device* pci_get_device(int index)
{
    if (index < 0 || index >= pci_device_count)
        return 0;

    return &devices[index];
}

const struct pci_device* pci_get_parent_bridge(
    const struct pci_device* device)
{
    const struct pci_device* best = NULL;
    uint16_t best_span = UINT16_MAX;

    if (!device)
        return NULL;

    for (int i = 0; i < pci_device_count; ++i)
    {
        const struct pci_device* bridge = &devices[i];
        uint16_t span;

        if (bridge == device ||
            bridge->segment_group != device->segment_group ||
            bridge->class_code != 0x06U ||
            bridge->subclass != 0x04U ||
            bridge->header_type != 1U ||
            bridge->primary_bus != bridge->bus ||
            bridge->secondary_bus <= bridge->primary_bus ||
            bridge->secondary_bus > bridge->subordinate_bus ||
            device->bus < bridge->secondary_bus ||
            device->bus > bridge->subordinate_bus)
            continue;

        /* A bridge cannot be its own ancestor. */
        if (bridge->bus == device->bus)
            continue;

        span = (uint16_t)bridge->subordinate_bus -
               (uint16_t)bridge->secondary_bus;
        if (span < best_span)
        {
            best = bridge;
            best_span = span;
        }
    }

    return best;
}

int pci_get_bar(const struct pci_device* device, uint8_t index,
                uint64_t* address, int* is_io, int* is_64bit)
{
    uint8_t header_type;
    uint8_t bar_count;
    uint8_t offset;
    uint32_t low;
    uint32_t high = 0;
    int wide = 0;

    /*
     * Address and I/O-vs-memory are required to safely use a BAR.
     * Width is optional metadata; existing drivers intentionally pass NULL
     * when they do not need to distinguish 32-bit from 64-bit BARs.
     */
    if (address)
        *address = 0;
    if (is_io)
        *is_io = 0;
    if (is_64bit)
        *is_64bit = 0;
    if (!device || !address || !is_io)
        return 0;

    header_type = (uint8_t)(pci_read_config8(device, 0x0EU) & 0x7FU);
    /* Type-0 endpoint headers have six BARs; type-1 PCI bridges have two. */
    if (header_type == 0U)
        bar_count = 6U;
    else if (header_type == 1U)
        bar_count = 2U;
    else
        return 0;
    if (index >= bar_count)
        return 0;

    /* A 64-bit BAR consumes the following BAR slot as its high dword. */
    for (uint8_t prior = 0; prior < index; ++prior)
    {
        uint32_t prior_low = pci_read_config32(
            device, (uint8_t)(0x10U + prior * 4U));
        if (prior_low != 0U && prior_low != 0xFFFFFFFFU &&
            !(prior_low & 1U) && ((prior_low >> 1) & 0x03U) == 0x02U)
        {
            if ((uint8_t)(prior + 1U) == index)
                return 0;
            ++prior;
        }
    }

    offset = (uint8_t)(0x10U + index * 4U);
    low = pci_read_config32(device, offset);
    if (low == 0U || low == 0xFFFFFFFFU)
        return 0;

    if (low & 1U)
    {
        *address = (uint64_t)(low & 0xFFFFFFFCU);
        *is_io = 1;
        return *address != 0U;
    }

    {
        uint32_t memory_type = (low >> 1) & 0x03U;
        if (memory_type == 0x03U)
            return 0;
        wide = memory_type == 0x02U;
    }
    if (wide)
    {
        if ((uint8_t)(index + 1U) >= bar_count)
            return 0;
        high = pci_read_config32(device, (uint8_t)(offset + 4U));
    }

    *address = ((uint64_t)high << 32) | (uint64_t)(low & 0xFFFFFFF0U);
    *is_io = 0;
    *is_64bit = wide;
    return *address != 0U;
}

/*
 * PCI BAR sizing is destructive to the BAR register for a short interval.
 * To avoid disrupting a live device, refuse unless I/O decode, memory decode,
 * and bus mastering are all disabled. Hold the configuration lock across
 * save/probe/restore so another CPU cannot interleave a config-space access.
 */
int pci_get_bar_size(const struct pci_device* device, uint8_t index,
                     uint64_t* size)
{
    uint64_t flags;
    uint64_t base = 0;
    uint64_t mask = 0;
    uint64_t measured_size = 0;
    uint8_t header_type;
    uint8_t bar_count;
    uint8_t offset;
    uint32_t command_status;
    uint32_t low;
    uint32_t high = 0;
    uint32_t mask_low;
    uint32_t mask_high = 0;
    uint32_t restored_low;
    uint32_t restored_high = 0;
    int is_io;
    int is_64bit = 0;
    int touched = 0;
    int valid = 0;

    if (size != 0)
        *size = 0;
    if (device == 0 || size == 0)
        return 0;

    flags = pci_lock_config();
    command_status = pci_read_device_config_locked(device, 0x04U);
    if ((command_status & 0x0007U) != 0U)
        goto done;

    header_type = (uint8_t)((pci_read_device_config_locked(device, 0x0CU) >> 16) & 0x7FU);
    if (header_type == 0U)
        bar_count = 6U;
    else if (header_type == 1U)
        bar_count = 2U;
    else
        goto done;
    if (index >= bar_count)
        goto done;

    /* Refuse an index that is the upper half of an earlier 64-bit BAR. */
    for (uint8_t prior = 0; prior < index; ++prior)
    {
        uint32_t prior_low = pci_read_device_config_locked(device, (uint8_t)(0x10U + prior * 4U));
        if (prior_low != 0U && prior_low != 0xFFFFFFFFU &&
            !(prior_low & 1U) && ((prior_low >> 1) & 0x03U) == 0x02U)
        {
            if ((uint8_t)(prior + 1U) == index)
                goto done;
            ++prior;
        }
    }

    offset = (uint8_t)(0x10U + index * 4U);
    low = pci_read_device_config_locked(device, offset);
    if (low == 0U || low == 0xFFFFFFFFU)
        goto done;

    is_io = (low & 1U) != 0U;
    if (is_io)
        base = (uint64_t)(low & 0xFFFFFFFCU);
    else
    {
        uint32_t memory_type = (low >> 1) & 0x03U;
        if (memory_type == 0x03U)
            goto done;
        is_64bit = memory_type == 0x02U;
        if (is_64bit)
        {
            if ((uint8_t)(index + 1U) >= bar_count)
                goto done;
            high = pci_read_device_config_locked(device, (uint8_t)(offset + 4U));
        }
        base = ((uint64_t)high << 32) |
               (uint64_t)(low & 0xFFFFFFF0U);
    }
    /* A zero base is valid while sizing an implemented but unassigned BAR. */

    /* Probe mask while decode and DMA are disabled; always restore original. */
    pci_write_device_config_locked(device, offset, 0xFFFFFFFFU);
    if (is_64bit)
        pci_write_device_config_locked(device, (uint8_t)(offset + 4U), 0xFFFFFFFFU);
    touched = 1;
    mask_low = pci_read_device_config_locked(device, offset);
    if (is_64bit)
        mask_high = pci_read_device_config_locked(device, (uint8_t)(offset + 4U));

    /* Restore upper half before low half so the original 64-bit BAR is whole. */
    if (is_64bit)
        pci_write_device_config_locked(device, (uint8_t)(offset + 4U), high);
    pci_write_device_config_locked(device, offset, low);
    restored_low = pci_read_device_config_locked(device, offset);
    if (is_64bit)
        restored_high = pci_read_device_config_locked(device, (uint8_t)(offset + 4U));
    touched = 0;
    if (restored_low != low || (is_64bit && restored_high != high)) {
        touched = 1;
        goto done;
    }

    if (is_io)
    {
        mask = (uint64_t)(mask_low & 0xFFFFFFFCU);
        measured_size = (uint32_t)(~(uint32_t)mask + 1U);
    }
    else if (is_64bit)
    {
        mask = ((uint64_t)mask_high << 32) |
               (uint64_t)(mask_low & 0xFFFFFFF0U);
        measured_size = ~mask + 1ULL;
    }
    else
    {
        mask = (uint64_t)(mask_low & 0xFFFFFFF0U);
        measured_size = (uint32_t)(~(uint32_t)mask + 1U);
    }

    if (measured_size < (is_io ? 4ULL : 16ULL) ||
        (measured_size & (measured_size - 1ULL)) != 0ULL ||
        (base & (measured_size - 1ULL)) != 0ULL ||
        measured_size - 1ULL > UINT64_MAX - base)
        goto done;
    /* A non-64-bit BAR cannot describe an address range above 4 GiB. */
    if (!is_64bit && base + measured_size - 1ULL > UINT32_MAX)
        goto done;

    *size = measured_size;
    valid = 1;

done:
    /* Defensive restoration if future validation is added after probing. */
    if (touched)
    {
        if (is_64bit)
            pci_write_device_config_locked(device, (uint8_t)(0x10U + (index + 1U) * 4U), high);
        pci_write_device_config_locked(device, (uint8_t)(0x10U + index * 4U), low);
    }
    pci_unlock_config(flags);
    return valid;
}

int pci_enable_io_space(const struct pci_device* device)
{
    if (!device) return 0;
    uint32_t command_status = pci_read_device_config(device, 0x04);
    uint16_t command = (uint16_t)command_status;
    if (command & 1U) return 1;
    command |= 1U;
    if (!pci_write_config16(device, 0x04U, command))
        return 0;
    return (pci_read_device_config(device, 0x04) & 1U) != 0;
}

#ifdef PCI_TEST
int pci_allocate_unassigned_bar(const struct pci_device* device, uint8_t index)
{
    (void)device;
    (void)index;
    return 0;
}
#else
static int pci_memory_map_allows_bar(const vanta_boot_info_t* boot,
                                     uint64_t base, uint64_t size)
{
    uint64_t end;
    if (!boot || size == 0U || size > UINT64_MAX - base)
        return 0;
    end = base + size;
    if ((boot->flags & VANTA_BOOT_FLAG_MEMORY_MAP_VALID) == 0U ||
        (boot->flags & VANTA_BOOT_FLAG_MEMORY_MAP_TRUNCATED) != 0U ||
        boot->memory_region_count == 0U ||
        boot->memory_region_count > VANTA_BOOT_INFO_MAX_MEMORY_REGIONS)
        return 0;
    for (uint32_t i = 0; i < boot->memory_region_count; ++i)
    {
        const vanta_memory_region_t* region = &boot->memory_regions[i];
        uint64_t region_end;
        if (region->length > UINT64_MAX - region->base)
            return 0;
        region_end = region->base + region->length;
        if (region_end <= base || region->base >= end)
            continue;
        if (region->type == VANTA_MEMORY_USABLE ||
            region->type == VANTA_MEMORY_BOOTLOADER_RECLAIMABLE ||
            region->type == VANTA_MEMORY_ACPI_RECLAIMABLE ||
            region->type == VANTA_MEMORY_ACPI_NVS ||
            region->type == VANTA_MEMORY_BAD)
            return 0;
    }
    if ((boot->flags & VANTA_BOOT_FLAG_FRAMEBUFFER_VALID) != 0U &&
        boot->framebuffer.pitch != 0U && boot->framebuffer.height != 0U)
    {
        uint64_t fb_size = (uint64_t)boot->framebuffer.pitch *
                           boot->framebuffer.height;
        if (fb_size <= UINT64_MAX - boot->framebuffer.address)
        {
            uint64_t fb_end = boot->framebuffer.address + fb_size;
            if (fb_end > base && boot->framebuffer.address < end)
                return 0;
        }
    }
    return 1;
}

static int pci_bar_address_conflicts(uint64_t base, uint64_t size)
{
    uint64_t end = base + size;
    uint64_t guard_start = base > 0x01000000ULL ?
                           base - 0x01000000ULL : 0U;
    uint64_t guard_end = end > UINT64_MAX - 0x01000000ULL ?
                         UINT64_MAX : end + 0x01000000ULL;
    for (int d = 0; d < pci_device_count; ++d)
    {
        for (uint8_t b = 0; b < 6U; ++b)
        {
            uint64_t address = 0;
            int is_io = 0;
            int is_64bit = 0;
            if (!pci_get_bar(&devices[d], b, &address, &is_io, &is_64bit))
                continue;
            (void)is_64bit;
            if (!is_io && address >= guard_start && address < guard_end)
                return 1;
        }
    }
    return 0;
}

/*
 * Firmware occasionally leaves a 64-bit PCI BAR unassigned. Allocate it only
 * from the verified non-RAM PCI aperture above the highest usable memory,
 * before the local-APIC/IOAPIC window. Refuse ambiguous maps or collisions.
 * The caller must retry pci_get_bar() and map the BAR only after success.
 */
int pci_allocate_unassigned_bar(const struct pci_device* device, uint8_t index)
{
    const vanta_boot_info_t* boot = kernel_get_boot_info();
    uint16_t command = 0U;
    uint32_t low;
    uint32_t high = 0U;
    uint8_t offset;
    uint64_t size = 0U;
    uint64_t highest_usable = 0U;
    uint64_t candidate;
    const uint64_t mmio_limit = 0xFEC00000ULL;
    int is_64bit;
    int success = 0;
    const char* failure = "BOOT_INFO";

    if (!device || !boot || index >= 6U ||
        boot->magic != VANTA_BOOT_INFO_MAGIC ||
        boot->version != VANTA_BOOT_INFO_VERSION ||
        boot->struct_size < sizeof(*boot) ||
        (boot->flags & VANTA_BOOT_FLAG_MEMORY_MAP_VALID) == 0U ||
        (boot->flags & VANTA_BOOT_FLAG_MEMORY_MAP_TRUNCATED) != 0U ||
        boot->memory_region_count == 0U ||
        boot->memory_region_count > VANTA_BOOT_INFO_MAX_MEMORY_REGIONS)
        goto fail_early;

    offset = (uint8_t)(0x10U + index * 4U);
    low = pci_read_config32(device, offset);
    if (low == 0U || low == 0xFFFFFFFFU || (low & 1U) != 0U)
    {
        failure = "BAR_NOT_MEMORY";
        goto fail_early;
    }
    uint32_t memory_type = (low >> 1U) & 0x03U;
    if (memory_type == 0x03U)
    {
        failure = "BAR_RESERVED_TYPE";
        goto fail_early;
    }
    is_64bit = memory_type == 0x02U;
    if (is_64bit)
    {
        if (index >= 5U)
        {
            failure = "BAR_INDEX";
            goto fail_early;
        }
        high = pci_read_config32(device, (uint8_t)(offset + 4U));
        if (high != 0U || (low & 0xFFFFFFF0U) != 0U)
        {
            failure = "BAR_ALREADY_ASSIGNED";
            goto fail_early;
        }
    }
    else if ((low & 0xFFFFFFF0U) != 0U)
    {
        failure = "BAR_ALREADY_ASSIGNED";
        goto fail_early;
    }

    command = pci_read_config16(device, 0x04U);
    if (!pci_write_config16(device, 0x04U, (uint16_t)(command & ~0x0007U)))
    {
        failure = "COMMAND_DISABLE";
        goto fail_early;
    }
    failure = "BAR_SIZE";
    if (!pci_get_bar_size(device, index, &size) || size == 0U ||
        size > mmio_limit)
        goto restore_command;

    for (uint32_t i = 0; i < boot->memory_region_count; ++i)
    {
        const vanta_memory_region_t* region = &boot->memory_regions[i];
        uint64_t region_end;
        if (region->length > UINT64_MAX - region->base)
        {
            failure = "MEMORY_MAP_RANGE";
            goto restore_command;
        }
        region_end = region->base + region->length;
        if ((region->type == VANTA_MEMORY_USABLE ||
             region->type == VANTA_MEMORY_BOOTLOADER_RECLAIMABLE) &&
            region_end > highest_usable)
            highest_usable = region_end;
    }
    if (highest_usable == 0U || highest_usable >= mmio_limit)
    {
        failure = "NO_RAM_TOP";
        goto restore_command;
    }

    candidate = (highest_usable + size - 1U) & ~(size - 1U);
    failure = "NO_CANDIDATE";
    while (candidate < mmio_limit && size <= mmio_limit - candidate)
    {
        if (pci_memory_map_allows_bar(boot, candidate, size) &&
            !pci_bar_address_conflicts(candidate, size))
        {
            uint32_t assigned_low =
                (uint32_t)candidate | (low & 0x0FU);
            if (is_64bit &&
                !pci_write_config32(device, (uint8_t)(offset + 4U),
                                    (uint32_t)(candidate >> 32U)))
            {
                failure = "WRITE_BAR_HIGH";
                goto restore_command;
            }
            if (!pci_write_config32(device, offset, assigned_low))
            {
                failure = "WRITE_BAR_LOW";
                goto restore_command;
            }
            uint64_t assigned_address = 0U;
            int assigned_io = 0;
            int assigned_64 = 0;
            if (pci_get_bar(device, index, &assigned_address,
                            &assigned_io, &assigned_64) &&
                !assigned_io && assigned_address == candidate &&
                assigned_64 == is_64bit)
            {
                success = 1;
                failure = "OK";
            }
            else
                failure = "VERIFY_BAR";
            break;
        }
        if (candidate > UINT64_MAX - size)
            break;
        candidate += size;
    }

restore_command:
    if (!pci_write_config16(device, 0x04U, command))
    {
        failure = "COMMAND_RESTORE";
        success = 0;
    }
    if (!success)
    {
        serial_write("VANTAOS_PCI_BAR_ALLOC_");
        serial_write(failure);
        serial_write("\n");
    }
    return success;

fail_early:
    serial_write("VANTAOS_PCI_BAR_ALLOC_");
    serial_write(failure);
    serial_write("\n");
    return 0;
}

#endif

int pci_enable_memory_space(const struct pci_device* device)
{
    if (!device) return 0;
    uint32_t command_status = pci_read_device_config(device, 0x04);
    uint16_t command = (uint16_t)command_status;
    if (command & (1U << 1)) return 1;
    command |= (1U << 1);
    pci_write_device_config_locked(device, 0x04,
                     (uint32_t)command);
    return (pci_read_device_config(device, 0x04) & (1U << 1)) != 0;
}

int pci_enable_bus_mastering(const struct pci_device* device)
{
    if (!device) return 0;
    uint32_t command_status = pci_read_device_config(device, 0x04);
    uint16_t command = (uint16_t)command_status;
    if (command & (1U << 2)) return 1;
    command |= (1U << 2);
    pci_write_device_config_locked(device, 0x04,
                     (uint32_t)command);
    return (pci_read_device_config(device, 0x04) & (1U << 2)) != 0;
}

int pci_find_class(uint8_t class_code, uint8_t subclass, int prog_if,
                   int start_index)
{
    if (start_index < 0) start_index = 0;
    for (int i = start_index; i < pci_device_count; ++i)
    {
        const struct pci_device* device = &devices[i];
        if (device->class_code == class_code && device->subclass == subclass &&
            (prog_if < 0 || device->prog_if == (uint8_t)prog_if))
            return i;
    }
    return -1;
}

int pci_get_bar0(const struct pci_device* device, uint32_t* bar)
{
    if (!device || !bar)
        return 0;

    uint32_t value = pci_read_config32(device, 0x10U);

    if (value & 1U)
        return 0;

    *bar = value & 0xFFFFFFF0U;
    return *bar != 0;
}

const char* pci_get_vendor_name(uint16_t vendor_id)
{
    switch (vendor_id)
    {
        case 0x10DE:
            return "NVIDIA";

        case 0x1022:
            return "AMD";

        case 0x8086:
            return "Intel";

        case 0x10EC:
            return "Realtek";

        case 0x1AF4:
            return "VirtIO";

        case 0x1234:
            return "QEMU";

        default:
            return "Unknown";
    }
}

const char* pci_get_device_name(
    uint16_t vendor_id,
    uint16_t device_id
)
{
    /*
     * NVIDIA
     */
    if (vendor_id == 0x10DE)
    {
        /*
         * RTX 3060.
         *
         * Multiple board variants can use different
         * device IDs, so this database will grow over time.
         */
        switch (device_id)
        {
            case 0x2503:
            case 0x2504:
            case 0x2507:
            case 0x2508:
            case 0x2509:
            case 0x2544:
            case 0x2548:
                return "GeForce RTX 3060";

            default:
                return "NVIDIA Graphics Device";
        }
    }

    /*
     * AMD.
     */
    if (vendor_id == 0x1022)
    {
        switch (device_id)
        {
            default:
                return "AMD Device";
        }
    }

    /*
     * Realtek Ethernet.
     */
    if (vendor_id == 0x10EC)
    {
        switch (device_id)
        {
            case 0x8125:
                return "2.5GbE Ethernet Controller";

            case 0x8168:
                return "Gigabit Ethernet Controller";

            default:
                return "Realtek Ethernet Controller";
        }
    }

    /*
     * Intel.
     */
    if (vendor_id == 0x8086)
        return "Intel Device";

    /*
     * QEMU.
     */
    if (vendor_id == 0x1234 &&
        device_id == 0x1111)
    {
        return "QEMU Virtual VGA";
    }

    return "Unknown Device";
}

const char* pci_get_class_name(
    uint8_t class_code,
    uint8_t subclass
)
{
    switch (class_code)
    {
        case 0x01:
            switch (subclass)
            {
                case 0x06:
                    return "SATA Storage";

                case 0x08:
                    return "NVMe Storage";

                default:
                    return "Storage Controller";
            }

        case 0x02:
            return "Network Controller";

        case 0x03:
            switch (subclass)
            {
                case 0x00:
                    return "Graphics";

                case 0x01:
                    return "Display";

                default:
                    return "Display Controller";
            }

        case 0x04:
            return "Multimedia";

        case 0x0C:
            switch (subclass)
            {
                case 0x03:
                    return "USB Controller";

                default:
                    return "Serial Bus Controller";
            }

        default:
            return "Other Device";
    }
}


int pci_write_config16(const struct pci_device *device, uint8_t offset,
                       uint16_t value)
{
    uint64_t flags;
    uint16_t actual;

    if (!device || offset > 254U || (offset & 1U) != 0U)
        return 0;
    flags = pci_lock_config();
    if (!pci_write_device_config16_locked(device, offset, value))
    {
        pci_unlock_config(flags);
        return 0;
    }
    actual = (uint16_t)(pci_read_device_config_locked(
        device, (uint8_t)(offset & 0xFCU)) >> ((offset & 2U) * 8U));
    pci_unlock_config(flags);
    return actual == value;
}

int pci_write_config32(const struct pci_device *device, uint8_t offset,
                       uint32_t value)
{
    uint64_t flags;
    uint32_t actual;

    if (!device || offset > 252U || (offset & 3U) != 0U)
        return 0;
    flags = pci_lock_config();
    if (!pci_write_device_config_locked(device, offset, value))
    {
        pci_unlock_config(flags);
        return 0;
    }
    actual = pci_read_device_config_locked(device, offset);
    pci_unlock_config(flags);
    return actual == value;
}

uint32_t pci_read_config32(const struct pci_device *device, uint8_t offset)
{
    if (!device || offset > 252U || (offset & 3U) != 0U)
        return 0xFFFFFFFFU;
    return pci_read_device_config(device, offset);
}

uint8_t pci_read_config8(const struct pci_device *device, uint8_t offset)
{
    uint32_t value;
    if (!device)
        return 0xFFU;
    value = pci_read_device_config(device, (uint8_t)(offset & 0xFCU));
    return (uint8_t)(value >> ((offset & 3U) * 8U));
}

uint16_t pci_read_config16(const struct pci_device *device, uint8_t offset)
{
    uint32_t value;
    if (!device || offset > 254U || (offset & 1U) != 0U)
        return 0xFFFFU;
    value = pci_read_device_config(device, (uint8_t)(offset & 0xFCU));
    return (uint16_t)(value >> ((offset & 2U) * 8U));
}

/* Find one conventional PCI capability without writing config space. */
int pci_find_capability(const struct pci_device *device, uint8_t capability_id,
                        uint8_t *out_offset)
{
    uint8_t header_type;
    uint8_t offset;
    uint64_t visited = 0;

    if (out_offset != 0)
        *out_offset = 0;
    if (device == 0 || out_offset == 0 || capability_id == 0U ||
        capability_id == 0xFFU)
        return 0;

    if ((pci_read_config16(device, 0x06U) & 0x0010U) == 0U)
        return 0;

    header_type = (uint8_t)(pci_read_config8(device, 0x0EU) & 0x7FU);
    if (header_type != 0U && header_type != 1U)
        return 0;

    offset = pci_read_config8(device, 0x34U);
    for (unsigned hops = 0; offset != 0U && hops < 48U; ++hops)
    {
        uint8_t id;
        uint8_t next;
        unsigned slot;

        if (offset < 0x40U || offset > 0xFCU || (offset & 0x03U) != 0U)
            return 0;
        slot = (unsigned)(offset - 0x40U) / 4U;
        if (slot >= 48U || (visited & (1ULL << slot)) != 0U)
            return 0;
        visited |= 1ULL << slot;

        id = pci_read_config8(device, offset);
        if (id == 0U || id == 0xFFU)
            return 0;
        if (id == capability_id)
        {
            *out_offset = offset;
            return 1;
        }
        next = pci_read_config8(device, (uint8_t)(offset + 1U));
        if (next == 0U)
            return 0;
        offset = next;
    }
    return 0;
}

#ifndef PCI_TEST
static void pci_ensure_mcfg(void)
{
    if (!pci_mcfg_initialized)
    {
        pci_mcfg_region_count = acpi_discover_pci_mcfg(
            pci_mcfg_regions, ACPI_MAX_MCFG_REGIONS);
        pci_mcfg_initialized = 1;
    }
}
#endif

int pci_read_config_extended32(const struct pci_device* device,
                               uint16_t offset, uint32_t* value)
{
    if (value)
        *value = 0xFFFFFFFFU;
    if (!device || !value || offset < 0x100U ||
        offset > 0xFFCU || (offset & 3U) != 0U)
        return 0;

#ifdef PCI_TEST
    return pci_test_ecam_read32(device->segment_group, device->bus,
                                device->slot, device->function, offset, value);
#else
    pci_ensure_mcfg();
    for (uint32_t i = 0; i < pci_mcfg_region_count; i++)
    {
        const acpi_mcfg_region_t* region = &pci_mcfg_regions[i];
        if (region->segment_group != device->segment_group ||
            device->bus < region->start_bus ||
            device->bus > region->end_bus)
            continue;

        uint64_t bus_offset =
            ((uint64_t)(device->bus - region->start_bus) << 20U) |
            ((uint64_t)device->slot << 15U) |
            ((uint64_t)device->function << 12U);
        if (region->base_address > UINT64_MAX - bus_offset - offset)
            return 0;
        uint64_t physical = region->base_address + bus_offset + offset;
        volatile uint32_t* mapped =
            (volatile uint32_t*)mmio_map_region(physical, sizeof(uint32_t));
        if (!mapped)
            return 0;
        *value = *mapped;
        (void)mmio_unmap_region((void*)mapped, sizeof(uint32_t));
        return 1;
    }
    return 0;
#endif
}

int pci_find_extended_capability(const struct pci_device* device,
                                 uint16_t capability_id,
                                 uint16_t* out_offset)
{
    uint32_t visited[30] = {0};
    uint16_t offset = 0x100U;

    if (out_offset)
        *out_offset = 0U;
    if (!device || !out_offset || capability_id == 0U ||
        capability_id == 0xFFFFU)
        return 0;

    for (uint32_t hops = 0U; hops < 960U; hops++)
    {
        uint32_t header = 0xFFFFFFFFU;
        uint16_t id;
        uint16_t next;
        uint32_t index;
        if (!pci_read_config_extended32(device, offset, &header))
            return 0;
        id = (uint16_t)(header & 0xFFFFU);
        if (id == 0U || id == 0xFFFFU)
            return 0;
        if (id == capability_id)
        {
            *out_offset = offset;
            return 1;
        }

        index = (uint32_t)(offset - 0x100U) / 4U;
        if (index >= 960U)
            return 0;
        uint32_t bit = 1U << (index & 31U);
        if (visited[index >> 5U] & bit)
            return 0;
        visited[index >> 5U] |= bit;

        next = (uint16_t)((header >> 20U) & 0x0FFFU);
        if (next == 0U)
            return 0;
        if (next < 0x100U || next > 0xFFCU || (next & 3U) != 0U)
            return 0;
        offset = next;
    }
    return 0;
}

