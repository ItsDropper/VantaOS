/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_PCI_MMIO_H
#define VANTA_GPU_PCI_MMIO_H

#include <stddef.h>
#include <stdint.h>
#include "gpu_pci_resources.h"
#include "gpu_mmio.h"

typedef enum gpu_pci_mmio_status {
    GPU_PCI_MMIO_OK = 0,
    GPU_PCI_MMIO_INVALID_ARGUMENT,
    GPU_PCI_MMIO_RESOURCE_INVALID,
    GPU_PCI_MMIO_BUS_MASTER_ACTIVE,
    GPU_PCI_MMIO_DECODE_FAILED,
    GPU_PCI_MMIO_MAPPING_FAILED,
    GPU_PCI_MMIO_RESTORE_FAILED
} gpu_pci_mmio_status_t;

typedef struct gpu_pci_mmio_mapping {
    const struct pci_device *device;
    gpu_mmio_region_t region;
    void *mapping;
    uint64_t physical_base;
    size_t mapped_length;
    uint16_t original_command;
    uint8_t bar_index;
    uint8_t enabled_memory_decode;
    uint8_t active;
} gpu_pci_mmio_mapping_t;

/*
 * Maps only the requested prefix of an MMIO BAR. audited_bar_size must come
 * from a trusted, device-specific profile or safe resource manager; this API
 * never guesses BAR size or performs destructive BAR sizing.
 */
gpu_pci_mmio_status_t gpu_pci_mmio_map(
    const struct pci_device *device, uint8_t bar_index,
    size_t audited_bar_size, size_t map_length,
    gpu_pci_mmio_mapping_t *out_mapping);
/* Unmaps exactly the acquired range and clears only decode this call enabled. */
gpu_pci_mmio_status_t gpu_pci_mmio_unmap(gpu_pci_mmio_mapping_t *mapping);

#endif
