/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_CHANNEL_RING_H
#define VANTA_NVX_GSP_CHANNEL_RING_H

#include "nvx_gsp_context_dma.h"
#include "nvx_gsp_memory_dma.h"
#include "nvx_gsp_userd.h"

#define NVX_GSP_CHANNEL_RING_TAG UINT32_C(0x47504652)

typedef enum nvx_gsp_channel_ring_stage {
    NVX_GSP_CHANNEL_RING_IDLE = 0,
    NVX_GSP_CHANNEL_RING_MEMORY_CREATE_PENDING,
    NVX_GSP_CHANNEL_RING_CONTEXT_CREATE_PENDING,
    NVX_GSP_CHANNEL_RING_MAP_PENDING,
    NVX_GSP_CHANNEL_RING_READY,
    NVX_GSP_CHANNEL_RING_UNMAP_PENDING,
    NVX_GSP_CHANNEL_RING_CONTEXT_DESTROY_PENDING,
    NVX_GSP_CHANNEL_RING_MEMORY_DESTROY_PENDING
} nvx_gsp_channel_ring_stage_t;

/*
 * Owns physically contiguous USERD-compatible host backing, its RM CTXDMA
 * object, and its NVOS46 mapping. gpu_virtual_address is the RM-returned
 * DMA mapping address, never a CPU pointer or a locally guessed GPUVA.
 *
 * Keep the object zero-initialized and RPC scratch/response buffers stable
 * until every pending operation is recovered. Destroy unmaps before freeing
 * CTXDMA and backing memory. references protect a live channel's ring.
 */
typedef struct nvx_gsp_channel_ring {
    nvx_gsp_os_memory_t memory;
    nvx_gsp_context_dma_t context_dma;
    nvx_gsp_dma_mapping_t mapping;
    uint32_t client_handle, device_handle, memory_handle, context_dma_handle;
    uint32_t subdevice_handle, entry_count, reference_state;
    size_t bytes, allocation_bytes;
    volatile uint32_t *entries;
    uint64_t gpu_virtual_address;
    uint8_t stage, memory_lease_held;
} nvx_gsp_channel_ring_t;

nvx_gsp_rmapi_status_t nvx_gsp_channel_ring_create(
    nvx_gsp_rpc_manager_t *, nvx_gsp_channel_ring_t *,
    uint32_t client, uint32_t device, uint32_t memory,
    uint32_t context_dma, uint32_t subdevice, uint32_t entry_count,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn, void *delay_context);

nvx_gsp_rmapi_status_t nvx_gsp_channel_ring_recover(
    nvx_gsp_rpc_manager_t *, nvx_gsp_channel_ring_t *,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn, void *delay_context);

nvx_gsp_rmapi_status_t nvx_gsp_channel_ring_acquire(
    nvx_gsp_channel_ring_t *);
nvx_gsp_rmapi_status_t nvx_gsp_channel_ring_release(
    nvx_gsp_channel_ring_t *);

nvx_gsp_rmapi_status_t nvx_gsp_channel_ring_destroy(
    nvx_gsp_rpc_manager_t *, nvx_gsp_channel_ring_t *,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn, void *delay_context);

#endif
