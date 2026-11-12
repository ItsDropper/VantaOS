/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_DMA_MAP_H
#define VANTA_NVX_GSP_DMA_MAP_H

#include "nvx_gsp_rmapi.h"

typedef enum nvx_gsp_dma_map_stage {
    NVX_GSP_DMA_MAP_IDLE = 0,
    NVX_GSP_DMA_MAP_MAPPING,
    NVX_GSP_DMA_MAP_MAP_RELEASE_PENDING,
    NVX_GSP_DMA_MAP_MAPPED,
    NVX_GSP_DMA_MAP_UNMAPPING,
    NVX_GSP_DMA_MAP_UNMAP_RELEASE_PENDING
} nvx_gsp_dma_map_stage_t;

typedef struct nvx_gsp_dma_mapping {
    nvx_gsp_map_memory_dma_t request;
    nvx_gsp_rmapi_result_t operation;
    uint64_t dma_offset;
    uint64_t completed_dma_offset;
    nvx_gsp_rmapi_status_t completed_status;
    uint8_t stage;
} nvx_gsp_dma_mapping_t;

/*
 * Maps a subrange of an RM memory object into an RM DMA object. The caller
 * owns both RM objects and must keep them alive until unmap completes.
 * Zero-initialize the state. On timeout, retain scratch/response storage and
 * call recover() before reusing it.
 */
nvx_gsp_rmapi_status_t nvx_gsp_dma_map(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_dma_mapping_t *mapping,
    const nvx_gsp_map_memory_dma_t *request,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

nvx_gsp_rmapi_status_t nvx_gsp_dma_unmap(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_dma_mapping_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

/* Resume a timed-out RPC or retry releasing its completed local RPC handle. */
nvx_gsp_rmapi_status_t nvx_gsp_dma_map_recover(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_dma_mapping_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

#endif
