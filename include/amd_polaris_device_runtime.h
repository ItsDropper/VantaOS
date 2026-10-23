/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_AMD_POLARIS_DEVICE_RUNTIME_H
#define VANTA_AMD_POLARIS_DEVICE_RUNTIME_H

#include "amd_polaris_gfx_runtime.h"
#include "gpu_pci_mmio.h"

#define AMD_POLARIS_DEVICE_MMIO_BYTES 0xD000U

typedef enum amd_polaris_device_status {
    AMD_POLARIS_DEVICE_OK = 0,
    AMD_POLARIS_DEVICE_INVALID_ARGUMENT,
    AMD_POLARIS_DEVICE_UNSUPPORTED,
    AMD_POLARIS_DEVICE_NOT_READY,
    AMD_POLARIS_DEVICE_MMIO_FAILED,
    AMD_POLARIS_DEVICE_ENGINE_FAILED,
    AMD_POLARIS_DEVICE_SHUTDOWN_FAILED
} amd_polaris_device_status_t;

/*
 * Owns the PCI BAR mapping and all GFX queue DMA allocations. This runtime
 * does not authenticate firmware: caller must have completed PSP/GFX firmware
 * loading and verified CP readiness before passing firmware_ready=1.
 */
typedef struct amd_polaris_device_runtime {
    const struct pci_device *pci_device;
    gpu_pci_mmio_mapping_t mapping;
    amd_polaris_gfx_runtime_t gfx;
    uint8_t initialized;
} amd_polaris_device_runtime_t;

/* Zero-initialize the object. Device must be quiesced and firmware ready. */
amd_polaris_device_status_t amd_polaris_device_runtime_init(
    amd_polaris_device_runtime_t *runtime, const struct pci_device *device,
    size_t ring_bytes, int firmware_ready, int device_quiesced);

amd_polaris_device_status_t amd_polaris_device_submit_fence(
    amd_polaris_device_runtime_t *runtime, uint32_t *out_fence);
amd_polaris_device_status_t amd_polaris_device_wait_fence(
    amd_polaris_device_runtime_t *runtime, uint32_t fence,
    unsigned timeout_iterations);

/* Quiesces the CP and disables owned bus mastering before unmapping BAR2. */
amd_polaris_device_status_t amd_polaris_device_runtime_shutdown(
    amd_polaris_device_runtime_t *runtime, int device_quiesced);

#endif
