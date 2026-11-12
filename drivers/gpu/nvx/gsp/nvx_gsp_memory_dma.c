/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_memory_dma.h"

#include <limits.h>

static nvx_gsp_rmapi_status_t validate_owners(
    const nvx_gsp_memory_object_t *memory,
    const nvx_gsp_context_dma_t *context_dma,
    uint64_t *allocation_size, uint64_t *allocation_limit)
{
    nvx_gsp_rmapi_status_t status;

    if (!memory || !context_dma ||
        memory->stage != NVX_GSP_MEMORY_CREATED ||
        !memory->remote_created || !memory->result_valid ||
        context_dma->stage != NVX_GSP_CONTEXT_DMA_CREATED ||
        context_dma->client_handle != memory->client_handle ||
        context_dma->params.h_memory != memory->memory_handle ||
        context_dma->device_handle != memory->parent_handle ||
        context_dma->params.reserved != 0U ||
        context_dma->params.offset > context_dma->params.limit)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    status = nvx_gsp_memory_object_get_range(
        memory, allocation_size, allocation_limit);
    if (status != NVX_GSP_RMAPI_OK)
        return status;

    if (context_dma->params.offset > *allocation_limit ||
        context_dma->params.limit > *allocation_limit ||
        context_dma->params.limit >= *allocation_size)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    return NVX_GSP_RMAPI_OK;
}

static int mapping_matches_owners(
    const nvx_gsp_dma_mapping_t *mapping,
    const nvx_gsp_memory_object_t *memory,
    const nvx_gsp_context_dma_t *context_dma)
{
    return mapping && memory && context_dma &&
        mapping->request.client == memory->client_handle &&
        mapping->request.device == context_dma->device_handle &&
        mapping->request.dma == context_dma->context_dma_handle &&
        mapping->request.memory == memory->memory_handle;
}

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
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_map_memory_dma_t request = {0};
    nvx_gsp_rmapi_status_t status;
    uint64_t allocation_size, allocation_limit, end;

    if (!manager || !mapping || !scratch || !response || !length ||
        offset > UINT64_MAX - length)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    status = validate_owners(memory, context_dma,
        &allocation_size, &allocation_limit);
    if (status != NVX_GSP_RMAPI_OK)
        return status;

    end = offset + length;
    if (end > allocation_size || offset < context_dma->params.offset ||
        end - 1U > context_dma->params.limit ||
        end - 1U > allocation_limit)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    request.client = memory->client_handle;
    request.device = context_dma->device_handle;
    request.dma = context_dma->context_dma_handle;
    request.memory = memory->memory_handle;
    request.offset = offset;
    request.length = length;
    request.flags = flags;
    request.flags2 = flags2;
    request.kind_override = kind_override;
    request.dma_offset = dma_offset;

    return nvx_gsp_dma_map(manager, mapping, &request,
        scratch, scratch_size, response, response_capacity,
        timeout_ms, poll_interval_ms, delay_ms, delay_context);
}

nvx_gsp_rmapi_status_t nvx_gsp_memory_dma_recover(
    nvx_gsp_rpc_manager_t *manager,
    const nvx_gsp_memory_object_t *memory,
    const nvx_gsp_context_dma_t *context_dma,
    nvx_gsp_dma_mapping_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    uint64_t allocation_size, allocation_limit;
    nvx_gsp_rmapi_status_t status;

    if (!manager || !mapping || !scratch || !response)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    status = validate_owners(memory, context_dma,
        &allocation_size, &allocation_limit);
    if (status != NVX_GSP_RMAPI_OK)
        return status;
    if (!mapping_matches_owners(mapping, memory, context_dma))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    return nvx_gsp_dma_map_recover(manager, mapping,
        scratch, scratch_size, response, response_capacity,
        timeout_ms, poll_interval_ms, delay_ms, delay_context);
}

nvx_gsp_rmapi_status_t nvx_gsp_memory_dma_unmap(
    nvx_gsp_rpc_manager_t *manager,
    const nvx_gsp_memory_object_t *memory,
    const nvx_gsp_context_dma_t *context_dma,
    nvx_gsp_dma_mapping_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    uint64_t allocation_size, allocation_limit;
    nvx_gsp_rmapi_status_t status;

    if (!manager || !mapping || !scratch || !response)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    status = validate_owners(memory, context_dma,
        &allocation_size, &allocation_limit);
    if (status != NVX_GSP_RMAPI_OK)
        return status;
    if (!mapping_matches_owners(mapping, memory, context_dma))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    return nvx_gsp_dma_unmap(manager, mapping,
        scratch, scratch_size, response, response_capacity,
        timeout_ms, poll_interval_ms, delay_ms, delay_context);
}
