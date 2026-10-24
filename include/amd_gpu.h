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
#include "amd_polaris.h"
#include "amd_polaris_device_runtime.h"

#include <stddef.h>

#define AMD_GPU_PCI_VENDOR_ID GPU_PCI_VENDOR_AMD

/*
 * Caller-owned binding for the real Polaris runtime. firmware_ready must only
 * be set after platform firmware authentication/loading and CP-ready checks.
 * The binding and embedded runtime must remain at stable addresses.
 */
typedef struct amd_gpu_runtime_binding {
    amd_polaris_device_runtime_t runtime;
    size_t ring_bytes;
    unsigned fence_timeout_iterations;
    uint8_t firmware_ready;
    uint8_t configured;
    uint8_t initialized;
    uint8_t fence_verified;
} amd_gpu_runtime_binding_t;

typedef struct amd_gpu_descriptor {
    gpu_pci_match_t pci;
    uint8_t firmware_required;
    uint8_t acceleration_ready;
    uint8_t memory_space_enabled;
    uint8_t mmio_aperture_enabled;
    uint16_t pci_command;
    gpu_pci_resource_inventory_t resources;
    amd_polaris_status_t polaris_profile_status;
    amd_polaris_profile_t polaris_profile;
} amd_gpu_descriptor_t;

/* Read-only PCI identity and assigned BAR inventory; no decode/DMA changes. */
int amd_gpu_match(const struct pci_device *device);
int amd_gpu_describe(const struct pci_device *device,
                     amd_gpu_descriptor_t *out_descriptor);
/* Configure before gpu_driver_start; binding storage remains caller-owned. */
int amd_gpu_configure_runtime(gpu_device_t *device,
                              amd_gpu_runtime_binding_t *binding,
                              size_t ring_bytes,
                              unsigned fence_timeout_iterations,
                              int authenticated_firmware_ready);
const gpu_driver_descriptor_t *amd_gpu_driver(void);

#endif
