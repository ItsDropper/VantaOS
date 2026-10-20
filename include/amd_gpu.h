/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_AMD_GPU_H
#define VANTA_AMD_GPU_H

#include "gpu_core.h"
#include "gpu_pci_resources.h"

#define AMD_GPU_PCI_VENDOR_ID GPU_PCI_VENDOR_AMD

typedef struct amd_gpu_descriptor {
    gpu_pci_match_t pci;
    uint8_t firmware_required;
    uint8_t acceleration_ready;
    uint8_t memory_space_enabled;
    uint8_t mmio_aperture_assigned;
    uint16_t pci_command;
    gpu_pci_resource_inventory_t resources;
} amd_gpu_descriptor_t;

/* Read-only PCI identity and assigned BAR inventory; no decode/DMA changes. */
int amd_gpu_match(const struct pci_device *device);
int amd_gpu_describe(const struct pci_device *device,
                     amd_gpu_descriptor_t *out_descriptor);
const gpu_driver_descriptor_t *amd_gpu_driver(void);

#endif
