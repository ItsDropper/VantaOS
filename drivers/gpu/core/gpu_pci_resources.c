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
    uint8_t bar_count;
    uint8_t consumed_upper = 0U;

    if (out_inventory == NULL)
        return 0;
    memset(out_inventory, 0, sizeof(*out_inventory));
    out_inventory->primary_mmio_index = GPU_PCI_BAR_NONE;

    if (device == NULL ||
        !gpu_pci_header_bar_count(device, &bar_count))
        return 0;

    for (uint8_t index = 0U; index < bar_count; ++index) {
        uint8_t offset = (uint8_t)(0x10U + index * 4U);
        uint32_t raw;
        uint64_t base = 0U;
        int is_io = 0;
        int is_64bit = 0;
        gpu_pci_bar_resource_t *resource;

        if (consumed_upper != 0U) {
            consumed_upper = 0U;
            continue;
        }

        raw = pci_read_config32(device, offset);
        if (raw == 0xFFFFFFFFU)
            return 0;

        if ((raw & 1U) == 0U) {
            uint32_t memory_type = (raw >> 1) & 0x03U;
            if (memory_type == 0x03U)
                return 0;
            if (memory_type == 0x02U) {
                if ((uint8_t)(index + 1U) >= bar_count)
                    return 0;
                consumed_upper = 1U;
            }
        }

        /* A zero address is unassigned; a 64-bit BAR still consumes its pair. */
        if ((raw & ((raw & 1U) != 0U ? ~3U : ~15U)) == 0U)
            continue;

        if (!pci_get_bar(device, index, &base, &is_io, &is_64bit))
            return 0;
        if ((is_io != 0 && is_io != 1) ||
            (is_64bit != 0 && is_64bit != 1))
            return 0;
        if (base == 0U)
            continue;
        if (is_io) {
            if ((base & 0x03U) != 0U)
                return 0;
        } else if ((base & 0x0FU) != 0U) {
            return 0;
        }

        if (out_inventory->count >= GPU_PCI_BAR_COUNT)
            return 0;
        resource = &out_inventory->bars[out_inventory->count++];
        resource->base = base;
        resource->index = index;
        resource->is_io = (uint8_t)is_io;
        resource->is_64bit = (uint8_t)is_64bit;
        resource->prefetchable =
            (uint8_t)(!is_io && ((raw & 0x08U) != 0U));

        if (!is_io && out_inventory->primary_mmio_index == GPU_PCI_BAR_NONE)
            out_inventory->primary_mmio_index = index;
    }

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
