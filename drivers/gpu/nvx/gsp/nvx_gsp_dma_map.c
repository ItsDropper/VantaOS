/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_dma_map.h"

#include <limits.h>
#include <string.h>

static nvx_gsp_rmapi_status_t finish_release(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_dma_mapping_t *mapping)
{
    nvx_gsp_rpc_status_t released =
        nvx_gsp_rpc_release(manager, &mapping->operation.handle);
    if (released != NVX_GSP_RPC_OK)
        return NVX_GSP_RMAPI_RPC_ERROR;

    if (mapping->stage == NVX_GSP_DMA_MAP_MAP_RELEASE_PENDING) {
        if (mapping->completed_status == NVX_GSP_RMAPI_OK) {
            mapping->dma_offset = mapping->completed_dma_offset;
            mapping->stage = NVX_GSP_DMA_MAP_MAPPED;
        } else {
            mapping->dma_offset = 0U;
            mapping->stage = NVX_GSP_DMA_MAP_IDLE;
        }
    } else if (mapping->stage == NVX_GSP_DMA_MAP_UNMAP_RELEASE_PENDING) {
        mapping->stage = mapping->completed_status == NVX_GSP_RMAPI_OK ?
            NVX_GSP_DMA_MAP_IDLE : NVX_GSP_DMA_MAP_MAPPED;
        if (mapping->stage == NVX_GSP_DMA_MAP_IDLE)
            mapping->dma_offset = 0U;
    } else {
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    }
    return mapping->completed_status;
}

static nvx_gsp_rmapi_status_t wait_and_release(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_dma_mapping_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    if (mapping->stage == NVX_GSP_DMA_MAP_MAP_RELEASE_PENDING ||
        mapping->stage == NVX_GSP_DMA_MAP_UNMAP_RELEASE_PENDING)
        return finish_release(manager, mapping);

    status = nvx_gsp_rmapi_wait(manager, &mapping->operation, scratch,
        scratch_size, response, response_capacity, timeout_ms,
        poll_interval_ms, delay_ms, delay_context);
    if (status == NVX_GSP_RMAPI_TIMEOUT)
        return status;

    mapping->completed_status = status;
    if (mapping->stage == NVX_GSP_DMA_MAP_MAPPING &&
        status == NVX_GSP_RMAPI_OK) {
        status = nvx_gsp_rm_map_memory_dma_result(&mapping->operation,
            response, mapping->operation.response_size,
            &mapping->completed_dma_offset);
        mapping->completed_status = status;
    }
    mapping->stage = mapping->stage == NVX_GSP_DMA_MAP_MAPPING ?
        NVX_GSP_DMA_MAP_MAP_RELEASE_PENDING :
        NVX_GSP_DMA_MAP_UNMAP_RELEASE_PENDING;
    return finish_release(manager, mapping);
}

nvx_gsp_rmapi_status_t nvx_gsp_dma_map(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_dma_mapping_t *mapping,
    const nvx_gsp_map_memory_dma_t *request,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_map_memory_dma_t wire;
    nvx_gsp_rmapi_status_t status;
    if (!manager || !mapping || !request || !scratch || !response ||
        !timeout_ms || !poll_interval_ms || !delay_ms ||
        !request->client || !request->device || !request->dma ||
        !request->memory || !request->length ||
        request->offset > UINT64_MAX - request->length ||
        mapping->stage != NVX_GSP_DMA_MAP_IDLE ||
        mapping->operation.handle.valid)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    wire = *request;
    wire.dma_offset = 0U;
    mapping->request = wire;
    mapping->completed_dma_offset = 0U;
    mapping->completed_status = NVX_GSP_RMAPI_OK;
    status = nvx_gsp_rm_map_memory_dma_submit(manager, &wire, scratch,
        scratch_size, response, response_capacity, &mapping->operation);
    if (status != NVX_GSP_RMAPI_OK)
        return status;
    mapping->stage = NVX_GSP_DMA_MAP_MAPPING;
    return wait_and_release(manager, mapping, scratch, scratch_size, response,
        response_capacity, timeout_ms, poll_interval_ms, delay_ms,
        delay_context);
}

nvx_gsp_rmapi_status_t nvx_gsp_dma_unmap(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_dma_mapping_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_unmap_memory_dma_t request;
    nvx_gsp_rmapi_status_t status;
    if (!manager || !mapping || !scratch || !response || !timeout_ms ||
        !poll_interval_ms || !delay_ms ||
        mapping->stage != NVX_GSP_DMA_MAP_MAPPED ||
        !mapping->request.length || !mapping->dma_offset ||
        mapping->operation.handle.valid)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    memset(&request, 0, sizeof(request));
    request.client = mapping->request.client;
    request.device = mapping->request.device;
    request.dma = mapping->request.dma;
    request.memory = mapping->request.memory;
    request.flags = mapping->request.flags;
    request.dma_offset = mapping->dma_offset;
    request.size = mapping->request.length;
    status = nvx_gsp_rm_unmap_memory_dma_submit(manager, &request, scratch,
        scratch_size, response, response_capacity, &mapping->operation);
    if (status != NVX_GSP_RMAPI_OK)
        return status;
    mapping->stage = NVX_GSP_DMA_MAP_UNMAPPING;
    return wait_and_release(manager, mapping, scratch, scratch_size, response,
        response_capacity, timeout_ms, poll_interval_ms, delay_ms,
        delay_context);
}

nvx_gsp_rmapi_status_t nvx_gsp_dma_map_recover(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_dma_mapping_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    if (!manager || !mapping || !scratch || !response || !timeout_ms ||
        !poll_interval_ms || !delay_ms ||
        (mapping->stage != NVX_GSP_DMA_MAP_MAPPING &&
         mapping->stage != NVX_GSP_DMA_MAP_MAP_RELEASE_PENDING &&
         mapping->stage != NVX_GSP_DMA_MAP_UNMAPPING &&
         mapping->stage != NVX_GSP_DMA_MAP_UNMAP_RELEASE_PENDING) ||
        !mapping->operation.handle.valid)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    return wait_and_release(manager, mapping, scratch, scratch_size, response,
        response_capacity, timeout_ms, poll_interval_ms, delay_ms,
        delay_context);
}
