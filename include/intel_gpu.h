/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_INTEL_GPU_H
#define VANTA_INTEL_GPU_H

#include "gpu_core.h"
#include "gpu_pci_resources.h"

#define INTEL_GPU_PCI_VENDOR_ID GPU_PCI_VENDOR_INTEL

typedef struct intel_gpu_descriptor {
    gpu_pci_match_t pci;
    uint8_t firmware_required;
    uint8_t acceleration_ready;
    uint8_t memory_space_enabled;
    uint8_t mmio_aperture_enabled;
    uint16_t pci_command;
    gpu_pci_resource_inventory_t resources;
} intel_gpu_descriptor_t;

/* Read-only PCI identity and assigned BAR inventory; no decode/DMA changes. */
int intel_gpu_match(const struct pci_device *device);
int intel_gpu_describe(const struct pci_device *device,
                       intel_gpu_descriptor_t *out_descriptor);
const gpu_driver_descriptor_t *intel_gpu_driver(void);

#endif
