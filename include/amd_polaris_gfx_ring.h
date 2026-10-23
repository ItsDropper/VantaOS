/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_AMD_POLARIS_GFX_RING_H
#define VANTA_AMD_POLARIS_GFX_RING_H

#include <stddef.h>
#include <stdint.h>
#include "amd_polaris_commands.h"
#include "gpu_mmio.h"

#define AMD_POLARIS_GFX_RING_MIN_BYTES 4096U
#define AMD_POLARIS_GFX_RING_MAX_BYTES (1024U * 1024U)
#define AMD_POLARIS_GFX_RING_FENCE_DWORDS AMD_POLARIS_PM4_FENCE_DWORDS

typedef enum amd_polaris_gfx_ring_status {
    AMD_POLARIS_GFX_RING_OK = 0,
    AMD_POLARIS_GFX_RING_INVALID_ARGUMENT,
    AMD_POLARIS_GFX_RING_NOT_QUIESCED,
    AMD_POLARIS_GFX_RING_FIRMWARE_NOT_READY,
    AMD_POLARIS_GFX_RING_MMIO_ERROR,
    AMD_POLARIS_GFX_RING_VERIFY_FAILED,
    AMD_POLARIS_GFX_RING_BUSY,
    AMD_POLARIS_GFX_RING_PACKET_REJECTED,
    AMD_POLARIS_GFX_RING_FENCE_TIMEOUT,
    AMD_POLARIS_GFX_RING_FAULTED
} amd_polaris_gfx_ring_status_t;

/*
 * Zero-initialize the runtime before first use. Owns no memory: ring_storage and fence storage remain caller-owned DMA
 * allocations. All GPU addresses must be device-visible, DMA-coherent, and
 * mapped in the active VM. Keep this object and its buffers at stable addresses.
 */
typedef struct amd_polaris_gfx_ring {
    const gpu_mmio_region_t *mmio;
    volatile uint32_t *ring_storage;
    size_t ring_bytes;
    uint64_t ring_gpu_address;
    uint32_t write_pointer;
    uint8_t vmid;
    uint8_t initialized;
    uint8_t faulted;
} amd_polaris_gfx_ring_t;

/*
 * GFX8/Polaris CP firmware must already be authenticated and running. The
 * device must be quiesced during setup. This routine configures the GFX ring
 * and starts the CP only after register readback verifies the ring addresses.
 */
amd_polaris_gfx_ring_status_t amd_polaris_gfx_ring_init(
    amd_polaris_gfx_ring_t *ring, const gpu_mmio_region_t *mmio,
    volatile uint32_t *ring_storage, uint64_t ring_gpu_address,
    size_t ring_bytes, uint64_t rptr_gpu_address, uint8_t vmid,
    int firmware_ready, int device_quiesced);

/* Submits only the exact, prevalidated single-dword WRITE_DATA fence packet. */
amd_polaris_gfx_ring_status_t amd_polaris_gfx_ring_submit_fence(
    amd_polaris_gfx_ring_t *ring, const uint32_t *packet, size_t packet_dwords,
    uint64_t allowed_fence_gpu_address, uint32_t expected_value);

/* Bounded polling. A timeout faults the queue; callers must quiesce before reuse. */
amd_polaris_gfx_ring_status_t amd_polaris_gfx_ring_wait_fence(
    amd_polaris_gfx_ring_t *ring, const volatile uint32_t *fence_cpu_address,
    uint32_t expected_value, unsigned timeout_iterations);

/* Does not free caller-owned DMA. It halts the CP only when the device is quiesced. */
amd_polaris_gfx_ring_status_t amd_polaris_gfx_ring_shutdown(
    amd_polaris_gfx_ring_t *ring, int device_quiesced);

#endif
