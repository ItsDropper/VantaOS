/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_pci_resources.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint32_t bar_words[6];
static uint8_t header_type;

uint8_t pci_read_config8(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    return offset == 0x0EU ? header_type : 0U;
}

uint32_t pci_read_config32(const struct pci_device *device, uint8_t offset)
{
    unsigned index;
    (void)device;
    if (offset < 0x10U || offset > 0x24U || ((offset - 0x10U) & 3U) != 0U)
        return 0xFFFFFFFFU;
    index = (unsigned)(offset - 0x10U) / 4U;
    return bar_words[index];
}

int pci_get_bar(const struct pci_device *device, uint8_t index,
                uint64_t *address, int *is_io, int *is_64bit)
{
    uint32_t low;
    uint64_t base;
    (void)device;

    if (address != 0)
        *address = 0U;
    if (is_io != 0)
        *is_io = 0;
    if (is_64bit != 0)
        *is_64bit = 0;
    if (index >= 6U || address == 0 || is_io == 0)
        return 0;

    low = bar_words[index];
    if (low == 0U || low == 0xFFFFFFFFU)
        return 0;
    if (low & 1U) {
        base = (uint64_t)(low & ~0x03U);
        *is_io = 1;
    } else {
        uint32_t type = (low >> 1) & 0x03U;
        if (type == 0x03U)
            return 0;
        base = (uint64_t)(low & ~0x0FU);
        if (type == 0x02U) {
            if (index + 1U >= 6U)
                return 0;
            base |= (uint64_t)bar_words[index + 1U] << 32;
            if (is_64bit != 0)
                *is_64bit = 1;
        }
    }
    *address = base;
    return 1;
}

static void reset_bars(void)
{
    memset(bar_words, 0, sizeof(bar_words));
    header_type = 0U;
}

int main(void)
{
    const struct pci_device gpu = {
        .vendor_id = 0x8086U, .device_id = 0x56A0U,
        .class_code = 0x03U, .subclass = 0x00U
    };
    gpu_pci_resource_inventory_t inventory;
    const gpu_pci_bar_resource_t *bar;

    reset_bars();
    /* 64-bit prefetchable MMIO BAR at 0x0000001200000000, consuming BAR1. */
    bar_words[0] = 0x0000000CU;
    bar_words[1] = 0x00000012U;
    /* I/O BAR and a separate 32-bit prefetchable MMIO BAR. */
    bar_words[2] = 0x0000C001U;
    bar_words[4] = 0xF0000008U;

    assert(gpu_pci_resources_scan(&gpu, &inventory));
    assert(inventory.count == 3U);
    assert(inventory.primary_mmio_index == 0U);
    bar = gpu_pci_resources_find_mmio(&inventory, 0U);
    assert(bar != 0);
    assert(bar->base == 0x1200000000ULL);
    assert(bar->is_64bit == 1U);
    assert(bar->prefetchable == 1U);
    assert(gpu_pci_resources_find_mmio(&inventory, 1U) == 0);
    bar = gpu_pci_resources_find_mmio(&inventory, 4U);
    assert(bar != 0);
    assert(bar->base == 0xF0000000ULL);
    assert(bar->is_64bit == 0U);
    assert(bar->prefetchable == 1U);
    assert(inventory.bars[1].index == 2U);
    assert(inventory.bars[1].is_io == 1U);

    reset_bars();
    header_type = 2U;
    assert(!gpu_pci_resources_scan(&gpu, &inventory));
    assert(inventory.count == 0U);
    assert(inventory.primary_mmio_index == GPU_PCI_BAR_NONE);

    reset_bars();
    /* Reserved memory BAR type must be rejected without partial output. */
    bar_words[0] = 0x00000006U;
    bar_words[2] = 0xE0000000U;
    assert(!gpu_pci_resources_scan(&gpu, &inventory));
    assert(inventory.count == 0U);
    assert(inventory.primary_mmio_index == GPU_PCI_BAR_NONE);

    reset_bars();
    /* A 64-bit BAR cannot consume a missing final high-dword slot. */
    header_type = 1U;
    bar_words[1] = 0x00000004U;
    assert(!gpu_pci_resources_scan(&gpu, &inventory));

    return 0;
}
