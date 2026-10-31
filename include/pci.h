/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef PCI_H
#define PCI_H

#include <stdint.h>

struct pci_device
{
    uint8_t bus;
    uint8_t slot;
    uint8_t function;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    /* PCI domain/segment; zero for legacy configuration-space devices. */
    uint16_t segment_group;

    /* Standard header metadata, retained for bus topology and driver matching. */
    uint8_t header_type;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;

    /* Type-1 bridge bus numbers; zero for non-bridge headers. */
    uint8_t primary_bus;
    uint8_t secondary_bus;
    uint8_t subordinate_bus;

    /* Decoded type-1 bridge forwarding windows (limits are inclusive). */
    uint64_t io_window_base;
    uint64_t io_window_limit;
    uint64_t memory_window_base;
    uint64_t memory_window_limit;
    uint64_t prefetch_window_base;
    uint64_t prefetch_window_limit;
    uint8_t io_window_valid;
    uint8_t memory_window_valid;
    uint8_t prefetch_window_valid;
};

void pci_initialize(void);
uint8_t pci_read_config8(const struct pci_device *device, uint8_t offset);
uint16_t pci_read_config16(const struct pci_device *device, uint8_t offset);
uint32_t pci_read_config32(const struct pci_device *device, uint8_t offset);
/* Exact-width writes for capability fields; readback is verified. */
int pci_write_config16(const struct pci_device *device, uint8_t offset, uint16_t value);
int pci_write_config32(const struct pci_device *device, uint8_t offset, uint32_t value);
/* Find a conventional capability (for example MSI 0x05 or MSI-X 0x11). */
int pci_find_capability(const struct pci_device *device, uint8_t capability_id,
                        uint8_t *out_offset);
/* PCIe extended config space (0x100-0xFFF), available with valid ACPI MCFG. */
int pci_read_config_extended32(const struct pci_device* device,
                               uint16_t offset, uint32_t* value);
int pci_find_extended_capability(const struct pci_device* device,
                                 uint16_t capability_id,
                                 uint16_t* out_offset);

/* Assign an implemented but unassigned BAR from a validated PCI MMIO hole. */
int pci_allocate_unassigned_bar(const struct pci_device* device, uint8_t index);
int pci_get_bar(const struct pci_device* device, uint8_t index,
                uint64_t* address, int* is_io, int* is_64bit);
/* Size a BAR only when I/O decode, memory decode, and bus mastering are disabled. */
int pci_get_bar_size(const struct pci_device* device, uint8_t index,
                     uint64_t* size);
int pci_enable_memory_space(const struct pci_device* device);
int pci_enable_io_space(const struct pci_device* device);
/* Enable bus mastering only after DMA buffers and device state are ready. */
int pci_enable_bus_mastering(const struct pci_device* device);
int pci_find_class(uint8_t class_code, uint8_t subclass, int prog_if, int start_index);
int pci_get_bar0(const struct pci_device* device, uint32_t* bar);

int pci_get_device_count(void);

const struct pci_device* pci_get_device(int index);

/*
 * Return the narrowest enumerated PCI-to-PCI bridge whose secondary bus
 * range contains the device. Returns NULL for root-bus devices or when
 * firmware bridge bus numbering is inconsistent.
 */
const struct pci_device* pci_get_parent_bridge(
    const struct pci_device* device);

const char* pci_get_vendor_name(uint16_t vendor_id);

const char* pci_get_device_name(
    uint16_t vendor_id,
    uint16_t device_id
);

const char* pci_get_class_name(
    uint8_t class_code,
    uint8_t subclass
);

#endif