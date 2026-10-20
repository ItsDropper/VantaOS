/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_PCI_RESOURCES_H
#define VANTA_GPU_PCI_RESOURCES_H

#include <stdint.h>
#include "pci.h"

#define GPU_PCI_BAR_COUNT 6U
#define GPU_PCI_BAR_NONE 0xFFU

typedef struct gpu_pci_bar_resource {
    uint64_t base;
    uint8_t index;
    uint8_t is_io;
    uint8_t is_64bit;
    uint8_t prefetchable;
} gpu_pci_bar_resource_t;

typedef struct gpu_pci_resource_inventory {
    gpu_pci_bar_resource_t bars[GPU_PCI_BAR_COUNT];
    uint8_t count;
    uint8_t primary_mmio_index;
} gpu_pci_resource_inventory_t;

/*
 * Read assigned BARs without sizing, remapping, enabling decode, or enabling
 * bus mastering. BAR sizes are intentionally omitted: sizing requires temporary
 * config writes and is not safe for an active device.
 */
int gpu_pci_resources_scan(const struct pci_device *device,
                           gpu_pci_resource_inventory_t *out_inventory);
const gpu_pci_bar_resource_t *gpu_pci_resources_find_mmio(
    const gpu_pci_resource_inventory_t *inventory, uint8_t index);

#endif
