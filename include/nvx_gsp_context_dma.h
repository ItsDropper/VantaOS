/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_CONTEXT_DMA_H
#define VANTA_NVX_GSP_CONTEXT_DMA_H

#include "nvx_gsp_rmapi.h"

/* NVIDIA public class/cl0002.h: NV01_CONTEXT_DMA. */
#define NVX_GSP_RM_CLASS_CONTEXT_DMA UINT32_C(0x00000002)

typedef enum nvx_gsp_context_dma_stage {
    NVX_GSP_CONTEXT_DMA_IDLE = 0,
    NVX_GSP_CONTEXT_DMA_ALLOC_PENDING,
    NVX_GSP_CONTEXT_DMA_ALLOC_RELEASE_PENDING,
    NVX_GSP_CONTEXT_DMA_CREATED,
    NVX_GSP_CONTEXT_DMA_FREE_PENDING,
    NVX_GSP_CONTEXT_DMA_FREE_RELEASE_PENDING
} nvx_gsp_context_dma_stage_t;

typedef struct nvx_gsp_context_dma_params {
    uint32_t h_subdevice;
    uint32_t flags;
    uint32_t h_memory;
    uint32_t reserved;
    uint64_t offset;
    uint64_t limit;
} nvx_gsp_context_dma_params_t;

typedef struct nvx_gsp_context_dma {
    uint32_t client_handle;
    uint32_t device_handle;
    uint32_t context_dma_handle;
    nvx_gsp_context_dma_params_t params;
    nvx_gsp_rmapi_result_t operation;
    nvx_gsp_rmapi_status_t completed_status;
    uint8_t stage;
} nvx_gsp_context_dma_t;

/*
 * Create a CTXDMA object over an existing RM memory object. limit is inclusive.
 * The caller owns h_memory and must keep it alive until this CTXDMA is freed.
 * Zero-initialize state. On timeout, keep RPC response storage alive and
 * recover the operation before reusing buffers or releasing the memory object.
 */
nvx_gsp_rmapi_status_t nvx_gsp_context_dma_create(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_context_dma_t *context_dma,
    uint32_t client_handle, uint32_t device_handle,
    uint32_t context_dma_handle, uint32_t memory_handle,
    uint32_t subdevice_handle, uint32_t flags,
    uint64_t offset, uint64_t limit,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

nvx_gsp_rmapi_status_t nvx_gsp_context_dma_destroy(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_context_dma_t *context_dma,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

/* Resume timed-out allocation/free or retry local RPC-handle release. */
nvx_gsp_rmapi_status_t nvx_gsp_context_dma_recover(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_context_dma_t *context_dma,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

#endif
