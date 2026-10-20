/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_AMD_GPU_H
#define VANTA_AMD_GPU_H

#include "gpu_core.h"

#define AMD_GPU_PCI_VENDOR_ID GPU_PCI_VENDOR_AMD

typedef struct amd_gpu_descriptor {
    gpu_pci_match_t pci;
    uint8_t firmware_required;
    uint8_t acceleration_ready;
} amd_gpu_descriptor_t;

/* Safe initial backend: identity and family detection only. */
int amd_gpu_match(const struct pci_device *device);
int amd_gpu_describe(const struct pci_device *device,
                     amd_gpu_descriptor_t *out_descriptor);
const gpu_driver_descriptor_t *amd_gpu_driver(void);

#endif
