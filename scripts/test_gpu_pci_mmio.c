/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_pci_mmio.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint16_t command;
static int fail_enable, fail_map, fail_unmap;
static unsigned maps, unmaps;
static uint8_t fake_regs[4096] __attribute__((aligned(4096)));
static const struct pci_device gpu = {
    .bus = 1U, .slot = 0U, .function = 0U,
    .vendor_id = 0x8086U, .device_id = 0x56A0U,
    .class_code = 0x03U, .subclass = 0x00U
};

uint8_t pci_read_config8(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    return offset == 0x0EU ? 0U : 0U;
}
uint16_t pci_read_config16(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    return offset == 0x04U ? command : 0U;
}
uint32_t pci_read_config32(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    if (offset == 0x10U) return 0xE0000000U;
    if (offset >= 0x10U && offset < 0x28U) return 0U;
    return 0U;
}
int pci_get_bar(const struct pci_device *device, uint8_t index,
                uint64_t *address, int *is_io, int *is_64bit)
{
    (void)device;
    if (index != 0U) return 0;
    *address = 0xE0000000ULL;
    *is_io = 0;
    *is_64bit = 0;
    return 1;
}
int pci_enable_memory_space(const struct pci_device *device)
{
    (void)device;
    if (fail_enable) return 0;
    command |= 0x0002U;
    return 1;
}
int pci_write_config16(const struct pci_device *device, uint8_t offset,
                       uint16_t value)
{
    (void)device;
    if (offset != 0x04U) return 0;
    command = value;
    return 1;
}
void *mmio_map_region(uint64_t physical_address, size_t length)
{
    ++maps;
    if (fail_map || physical_address != 0xE0000000ULL ||
        length > sizeof(fake_regs))
        return NULL;
    return fake_regs;
}
int mmio_unmap_region(void *address, size_t length)
{
    ++unmaps;
    if (fail_unmap || address != fake_regs || length > sizeof(fake_regs))
        return 0;
    return 1;
}

int main(void)
{
    gpu_pci_mmio_mapping_t mapping = {0};

    assert(gpu_pci_mmio_map(NULL, 0U, 4096U, 4096U, &mapping) ==
           GPU_PCI_MMIO_INVALID_ARGUMENT);
    assert(gpu_pci_mmio_map(&gpu, 0U, 4096U, 0U, &mapping) ==
           GPU_PCI_MMIO_INVALID_ARGUMENT);
    assert(gpu_pci_mmio_map(&gpu, 0U, 2048U, 4096U, &mapping) ==
           GPU_PCI_MMIO_INVALID_ARGUMENT);
    command = 0x0000U;
    assert(gpu_pci_mmio_map(&gpu, 0U, 4096U, 4096U, &mapping) ==
           GPU_PCI_MMIO_OK);
    assert(mapping.active && mapping.region.length == 4096U);
    assert((command & 0x0002U) != 0U && (command & 0x0004U) == 0U);
    assert(gpu_mmio_write32(&mapping.region, 0U, 0x12345678U) == GPU_MMIO_OK);
    assert(gpu_mmio_read32(&mapping.region, 0U, (uint32_t *)&command) ==
           GPU_MMIO_OK);
    assert(gpu_pci_mmio_unmap(&mapping) == GPU_PCI_MMIO_OK);
    assert(!mapping.active && command == 0U && maps == 1U && unmaps == 1U);

    command = 0x0004U;
    assert(gpu_pci_mmio_map(&gpu, 0U, 4096U, 4096U, &mapping) ==
           GPU_PCI_MMIO_BUS_MASTER_ACTIVE);
    command = 0U;
    fail_enable = 1;
    assert(gpu_pci_mmio_map(&gpu, 0U, 4096U, 4096U, &mapping) ==
           GPU_PCI_MMIO_DECODE_FAILED);
    fail_enable = 0;
    fail_map = 1;
    assert(gpu_pci_mmio_map(&gpu, 0U, 4096U, 4096U, &mapping) ==
           GPU_PCI_MMIO_MAPPING_FAILED);
    assert(command == 0U);
    fail_map = 0;

    command = 0x0002U;
    assert(gpu_pci_mmio_map(&gpu, 0U, 4096U, 2048U, &mapping) ==
           GPU_PCI_MMIO_OK);
    fail_unmap = 1;
    assert(gpu_pci_mmio_unmap(&mapping) == GPU_PCI_MMIO_MAPPING_FAILED);
    assert(mapping.active);
    fail_unmap = 0;
    assert(gpu_pci_mmio_unmap(&mapping) == GPU_PCI_MMIO_OK);
    assert(command == 0x0002U);
    return 0;
}
