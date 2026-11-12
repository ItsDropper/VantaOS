/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_MEMORY_DMA_H
#define VANTA_NVX_GSP_MEMORY_DMA_H

#include "nvx_gsp_memory.h"
#include "nvx_gsp_context_dma.h"
#include "nvx_gsp_dma_map.h"

/*
 * Bind an already-created RM system-memory allocation to an already-created
 * CTXDMA object and map a bounded subrange through NVOS46.
 *
 * The CTXDMA object must reference memory->memory_handle. Its inclusive
 * [offset, limit] range must be contained in the validated RM allocation.
 * NVOS46 offsets are relative to the RM memory object, not the CTXDMA range.
 * Both RM objects must remain alive until unmap has completed.
 */
nvx_gsp_rmapi_status_t nvx_gsp_memory_dma_map(
    nvx_gsp_rpc_manager_t *manager,
    const nvx_gsp_memory_object_t *memory,
    const nvx_gsp_context_dma_t *context_dma,
    nvx_gsp_dma_mapping_t *mapping,
    uint64_t offset, uint64_t length,
    uint32_t flags, uint32_t flags2, uint32_t kind_override,
    uint64_t dma_offset,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

/* Retry a timed-out NVOS46/NVOS47 operation without releasing either RM owner. */
nvx_gsp_rmapi_status_t nvx_gsp_memory_dma_recover(
    nvx_gsp_rpc_manager_t *manager,
    const nvx_gsp_memory_object_t *memory,
    const nvx_gsp_context_dma_t *context_dma,
    nvx_gsp_dma_mapping_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

/* Unmap first; callers may free CTXDMA and memory only after this succeeds. */
nvx_gsp_rmapi_status_t nvx_gsp_memory_dma_unmap(
    nvx_gsp_rpc_manager_t *manager,
    const nvx_gsp_memory_object_t *memory,
    const nvx_gsp_context_dma_t *context_dma,
    nvx_gsp_dma_mapping_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

#endif
