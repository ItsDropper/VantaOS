/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_AMD_POLARIS_GFX_RUNTIME_H
#define VANTA_AMD_POLARIS_GFX_RUNTIME_H

#include "amd_polaris_gfx_ring.h"
#include "gpu_dma.h"
#include "pci.h"

typedef enum amd_polaris_gfx_runtime_status {
    AMD_POLARIS_GFX_RUNTIME_OK = 0,
    AMD_POLARIS_GFX_RUNTIME_INVALID_ARGUMENT,
    AMD_POLARIS_GFX_RUNTIME_NOT_READY,
    AMD_POLARIS_GFX_RUNTIME_ALLOCATION_FAILED,
    AMD_POLARIS_GFX_RUNTIME_PCI_ERROR,
    AMD_POLARIS_GFX_RUNTIME_RING_ERROR,
    AMD_POLARIS_GFX_RUNTIME_BUSY,
    AMD_POLARIS_GFX_RUNTIME_FENCE_TIMEOUT,
    AMD_POLARIS_GFX_RUNTIME_SHUTDOWN_FAILED
} amd_polaris_gfx_runtime_status_t;

/*
 * Owns ring, RPTR writeback and fence DMA allocations. Firmware must already
 * be authenticated/running; the device must be quiesced during init/shutdown.
 * If init returns an error with initialized set, call shutdown after ensuring
 * the device is quiesced: DMA is retained when CP stop cannot be verified.
 */
typedef struct amd_polaris_gfx_runtime {
    const struct pci_device *pci_device;
    const gpu_mmio_region_t *mmio;
    gpu_dma_buffer_t ring_dma;
    gpu_dma_buffer_t rptr_dma;
    gpu_dma_buffer_t fence_dma;
    amd_polaris_gfx_ring_t ring;
    uint16_t original_pci_command;
    uint32_t next_fence;
    uint32_t pending_fence;
    uint8_t initialized;
    uint8_t faulted;
    uint8_t pending;
    uint8_t bus_master_enabled_by_us;
} amd_polaris_gfx_runtime_t;

/* Zero-initialize runtime. Allocates all DMA before enabling bus mastering. */
amd_polaris_gfx_runtime_status_t amd_polaris_gfx_runtime_init(
    amd_polaris_gfx_runtime_t *runtime, const struct pci_device *pci_device,
    const gpu_mmio_region_t *mmio, size_t ring_bytes,
    int firmware_ready, int device_quiesced);

/* Single-flight fence submission; returned value must be passed to wait_fence. */
amd_polaris_gfx_runtime_status_t amd_polaris_gfx_runtime_submit_fence(
    amd_polaris_gfx_runtime_t *runtime, uint32_t *out_fence_value);
amd_polaris_gfx_runtime_status_t amd_polaris_gfx_runtime_wait_fence(
    amd_polaris_gfx_runtime_t *runtime, uint32_t fence_value,
    unsigned timeout_iterations);

/* Stops CP, restores bus-master state owned by this runtime, then frees DMA. */
amd_polaris_gfx_runtime_status_t amd_polaris_gfx_runtime_shutdown(
    amd_polaris_gfx_runtime_t *runtime, int device_quiesced);

#endif
