/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_pci_mmio.h"

#include <string.h>
#include "mmio.h"

#define GPU_PCI_COMMAND_MEMORY_SPACE 0x0002U
#define GPU_PCI_COMMAND_BUS_MASTER  0x0004U

static void gpu_pci_mmio_clear(gpu_pci_mmio_mapping_t *mapping)
{
    memset(mapping, 0, sizeof(*mapping));
}

static int gpu_pci_mmio_restore_decode(const struct pci_device *device,
                                       uint16_t original_command,
                                       int enabled_by_us)
{
    uint16_t current;

    if (!enabled_by_us)
        return 1;
    current = pci_read_config16(device, 0x04U);
    if (current == 0xFFFFU)
        return 0;
    /* Preserve unrelated command bits and never alter bus-master state. */
    current = (uint16_t)(current & (uint16_t)~GPU_PCI_COMMAND_MEMORY_SPACE);
    if ((original_command & GPU_PCI_COMMAND_MEMORY_SPACE) != 0U)
        current |= GPU_PCI_COMMAND_MEMORY_SPACE;
    return pci_write_config16(device, 0x04U, current) &&
           ((pci_read_config16(device, 0x04U) &
             GPU_PCI_COMMAND_MEMORY_SPACE) ==
            (original_command & GPU_PCI_COMMAND_MEMORY_SPACE));
}

gpu_pci_mmio_status_t gpu_pci_mmio_map(
    const struct pci_device *device, uint8_t bar_index,
    size_t audited_bar_size, size_t map_length,
    gpu_pci_mmio_mapping_t *out_mapping)
{
    gpu_pci_resource_inventory_t inventory;
    const gpu_pci_bar_resource_t *bar;
    uint16_t command;
    int enabled_by_us = 0;
    void *mapped;

    if (out_mapping == NULL)
        return GPU_PCI_MMIO_INVALID_ARGUMENT;
    if (out_mapping->active != 0U || out_mapping->mapping != NULL)
        return GPU_PCI_MMIO_INVALID_ARGUMENT;
    gpu_pci_mmio_clear(out_mapping);

    if (device == NULL || bar_index >= GPU_PCI_BAR_COUNT ||
        audited_bar_size < 4U || map_length == 0U ||
        map_length > audited_bar_size)
        return GPU_PCI_MMIO_INVALID_ARGUMENT;
    if (!gpu_pci_resources_scan(device, &inventory))
        return GPU_PCI_MMIO_RESOURCE_INVALID;
    bar = gpu_pci_resources_find_mmio(&inventory, bar_index);
    if (bar == NULL || bar->is_io != 0U || bar->base == 0U ||
        bar->base > UINT64_MAX - ((uint64_t)map_length - 1U))
        return GPU_PCI_MMIO_RESOURCE_INVALID;

    command = pci_read_config16(device, 0x04U);
    if (command == 0xFFFFU)
        return GPU_PCI_MMIO_RESOURCE_INVALID;
    if ((command & GPU_PCI_COMMAND_BUS_MASTER) != 0U)
        return GPU_PCI_MMIO_BUS_MASTER_ACTIVE;

    if ((command & GPU_PCI_COMMAND_MEMORY_SPACE) == 0U) {
        if (!pci_enable_memory_space(device)) {
            if (!gpu_pci_mmio_restore_decode(device, command, 1))
                return GPU_PCI_MMIO_RESTORE_FAILED;
            return GPU_PCI_MMIO_DECODE_FAILED;
        }
        enabled_by_us = 1;
        {
            uint16_t observed = pci_read_config16(device, 0x04U);
            if (observed == 0xFFFFU ||
                (observed & GPU_PCI_COMMAND_MEMORY_SPACE) == 0U ||
                (observed & GPU_PCI_COMMAND_BUS_MASTER) != 0U) {
                if (!gpu_pci_mmio_restore_decode(device, command, enabled_by_us))
                    return GPU_PCI_MMIO_RESTORE_FAILED;
                return GPU_PCI_MMIO_DECODE_FAILED;
            }
        }
    }

    mapped = mmio_map_region(bar->base, map_length);
    if (mapped == NULL) {
        if (!gpu_pci_mmio_restore_decode(device, command, enabled_by_us))
            return GPU_PCI_MMIO_RESTORE_FAILED;
        return GPU_PCI_MMIO_MAPPING_FAILED;
    }

    out_mapping->device = device;
    out_mapping->mapping = mapped;
    out_mapping->region.base = (volatile uint8_t *)mapped;
    out_mapping->region.length = map_length;
    out_mapping->physical_base = bar->base;
    out_mapping->mapped_length = map_length;
    out_mapping->original_command = command;
    out_mapping->bar_index = bar_index;
    out_mapping->enabled_memory_decode = (uint8_t)enabled_by_us;
    out_mapping->active = 1U;
    return GPU_PCI_MMIO_OK;
}

gpu_pci_mmio_status_t gpu_pci_mmio_unmap(gpu_pci_mmio_mapping_t *mapping)
{
    gpu_pci_mmio_mapping_t saved;

    if (mapping == NULL || mapping->active == 0U ||
        mapping->device == NULL || mapping->mapping == NULL ||
        mapping->mapped_length == 0U ||
        mapping->region.base != (volatile uint8_t *)mapping->mapping ||
        mapping->region.length != mapping->mapped_length)
        return GPU_PCI_MMIO_INVALID_ARGUMENT;

    saved = *mapping;
    if (!mmio_unmap_region(saved.mapping, saved.mapped_length))
        return GPU_PCI_MMIO_MAPPING_FAILED;

    gpu_pci_mmio_clear(mapping);
    if (!gpu_pci_mmio_restore_decode(saved.device, saved.original_command,
                                     saved.enabled_memory_decode != 0U))
        return GPU_PCI_MMIO_RESTORE_FAILED;
    return GPU_PCI_MMIO_OK;
}
