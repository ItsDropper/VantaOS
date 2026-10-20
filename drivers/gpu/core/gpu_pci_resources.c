/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_pci_resources.h"

#include <stddef.h>
#include <string.h>

static int gpu_pci_header_bar_count(const struct pci_device *device,
                                    uint8_t *out_count)
{
    uint8_t type;

    if (device == NULL || out_count == NULL)
        return 0;

    type = (uint8_t)(pci_read_config8(device, 0x0EU) & 0x7FU);
    if (type == 0U)
        *out_count = 6U;
    else if (type == 1U)
        *out_count = 2U;
    else
        return 0;
    return 1;
}

int gpu_pci_resources_scan(const struct pci_device *device,
                           gpu_pci_resource_inventory_t *out_inventory)
{
    gpu_pci_resource_inventory_t inventory;
    uint8_t bar_count;
    uint8_t consumed_upper = 0U;

    if (out_inventory == NULL)
        return 0;
    memset(out_inventory, 0, sizeof(*out_inventory));
    out_inventory->primary_mmio_index = GPU_PCI_BAR_NONE;
    memset(&inventory, 0, sizeof(inventory));
    inventory.primary_mmio_index = GPU_PCI_BAR_NONE;

    if (device == NULL ||
        !gpu_pci_header_bar_count(device, &bar_count))
        return 0;

    for (uint8_t index = 0U; index < bar_count; ++index) {
        uint8_t offset = (uint8_t)(0x10U + index * 4U);
        uint32_t raw;
        uint32_t address_mask;
        uint64_t base = 0U;
        int is_io = 0;
        int is_64bit = 0;
        int expected_is_io;
        int expected_is_64bit = 0;
        gpu_pci_bar_resource_t *resource;

        if (consumed_upper != 0U) {
            consumed_upper = 0U;
            continue;
        }

        raw = pci_read_config32(device, offset);
        if (raw == 0xFFFFFFFFU)
            return 0;

        is_io = (raw & 1U) != 0U;
        expected_is_io = is_io;
        if (!is_io) {
            uint32_t memory_type = (raw >> 1) & 0x03U;
            if (memory_type == 0x03U)
                return 0;
            is_64bit = memory_type == 0x02U;
            expected_is_64bit = is_64bit;
            if (is_64bit) {
                uint32_t high;
                if ((uint8_t)(index + 1U) >= bar_count)
                    return 0;
                high = pci_read_config32(device, (uint8_t)(offset + 4U));
                if (high == 0xFFFFFFFFU)
                    return 0;
                consumed_upper = 1U;
                if ((raw & ~0x0FU) == 0U && high == 0U)
                    continue;
            } else if ((raw & ~0x0FU) == 0U) {
                continue;
            }
            address_mask = ~0x0FU;
        } else {
            if ((raw & ~0x03U) == 0U)
                continue;
            address_mask = ~0x03U;
        }

        if (!pci_get_bar(device, index, &base, &is_io, &is_64bit))
            return 0;
        if ((is_io != 0 && is_io != 1) ||
            (is_64bit != 0 && is_64bit != 1) ||
            is_io != expected_is_io || is_64bit != expected_is_64bit ||
            (is_io != 0 && is_64bit != 0))
            return 0;
        if (base == 0U)
            return 0;
        if ((base & (is_io ? 0x03U : 0x0FU)) != 0U)
            return 0;
        if ((raw & address_mask) == 0U && is_64bit == 0)
            return 0;

        if (inventory.count >= GPU_PCI_BAR_COUNT)
            return 0;
        resource = &inventory.bars[inventory.count++];
        resource->base = base;
        resource->index = index;
        resource->is_io = (uint8_t)is_io;
        resource->is_64bit = (uint8_t)is_64bit;
        resource->prefetchable =
            (uint8_t)(!is_io && ((raw & 0x08U) != 0U));

        if (!is_io && inventory.primary_mmio_index == GPU_PCI_BAR_NONE)
            inventory.primary_mmio_index = index;
    }

    *out_inventory = inventory;
    return 1;
}

const gpu_pci_bar_resource_t *gpu_pci_resources_find_mmio(
    const gpu_pci_resource_inventory_t *inventory, uint8_t index)
{
    if (inventory == NULL || inventory->count > GPU_PCI_BAR_COUNT)
        return NULL;

    for (uint8_t i = 0U; i < inventory->count; ++i) {
        const gpu_pci_bar_resource_t *resource = &inventory->bars[i];
        if (resource->index == index && !resource->is_io)
            return resource;
    }
    return NULL;
}
