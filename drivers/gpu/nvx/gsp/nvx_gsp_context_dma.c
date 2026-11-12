/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_context_dma.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(nvx_gsp_context_dma_params_t) == 32U,
    "NV_CONTEXT_DMA_ALLOCATION_PARAMS must match the NVIDIA 64-bit ABI");
_Static_assert(offsetof(nvx_gsp_context_dma_params_t, offset) == 16U,
    "NV_CONTEXT_DMA_ALLOCATION_PARAMS.offset ABI mismatch");
_Static_assert(offsetof(nvx_gsp_context_dma_params_t, limit) == 24U,
    "NV_CONTEXT_DMA_ALLOCATION_PARAMS.limit ABI mismatch");

static int state_valid(const nvx_gsp_context_dma_t *object)
{
    if (!object || object->stage > NVX_GSP_CONTEXT_DMA_FREE_RELEASE_PENDING)
        return 0;
    switch ((nvx_gsp_context_dma_stage_t)object->stage) {
    case NVX_GSP_CONTEXT_DMA_IDLE:
        return !object->operation.handle.valid;
    case NVX_GSP_CONTEXT_DMA_ALLOC_PENDING:
    case NVX_GSP_CONTEXT_DMA_ALLOC_RELEASE_PENDING:
        return object->operation.handle.valid &&
            object->operation.function == NVX_GSP_RPC_FN_RM_ALLOC;
    case NVX_GSP_CONTEXT_DMA_CREATED:
        return !object->operation.handle.valid && object->client_handle &&
            object->device_handle && object->context_dma_handle &&
            object->params.h_memory && object->params.limit >= object->params.offset;
    case NVX_GSP_CONTEXT_DMA_FREE_PENDING:
    case NVX_GSP_CONTEXT_DMA_FREE_RELEASE_PENDING:
        return object->operation.handle.valid &&
            object->operation.function == NVX_GSP_RPC_FN_FREE;
    default:
        return 0;
    }
}

static nvx_gsp_rmapi_status_t finish_release(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_context_dma_t *object)
{
    if (nvx_gsp_rpc_release(manager, &object->operation.handle) !=
        NVX_GSP_RPC_OK)
        return NVX_GSP_RMAPI_RPC_ERROR;

    if (object->stage == NVX_GSP_CONTEXT_DMA_ALLOC_RELEASE_PENDING) {
        object->stage = object->completed_status == NVX_GSP_RMAPI_OK ?
            NVX_GSP_CONTEXT_DMA_CREATED : NVX_GSP_CONTEXT_DMA_IDLE;
    } else if (object->stage == NVX_GSP_CONTEXT_DMA_FREE_RELEASE_PENDING) {
        object->stage = object->completed_status == NVX_GSP_RMAPI_OK ?
            NVX_GSP_CONTEXT_DMA_IDLE : NVX_GSP_CONTEXT_DMA_CREATED;
    } else {
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    }
    return object->completed_status;
}

static nvx_gsp_rmapi_status_t wait_operation(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_context_dma_t *object,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    if (object->stage == NVX_GSP_CONTEXT_DMA_ALLOC_RELEASE_PENDING ||
        object->stage == NVX_GSP_CONTEXT_DMA_FREE_RELEASE_PENDING)
        return finish_release(manager, object);

    status = nvx_gsp_rmapi_wait(manager, &object->operation, scratch,
        scratch_size, response, response_capacity, timeout_ms,
        poll_interval_ms, delay_ms, delay_context);
    /* Transport failure does not prove the RPC is no longer in flight. */
    if (status == NVX_GSP_RMAPI_TIMEOUT || status == NVX_GSP_RMAPI_RPC_ERROR)
        return status;

    object->completed_status = status;
    object->stage = object->stage == NVX_GSP_CONTEXT_DMA_ALLOC_PENDING ?
        NVX_GSP_CONTEXT_DMA_ALLOC_RELEASE_PENDING :
        NVX_GSP_CONTEXT_DMA_FREE_RELEASE_PENDING;
    return finish_release(manager, object);
}

nvx_gsp_rmapi_status_t nvx_gsp_context_dma_create(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_context_dma_t *object,
    uint32_t client_handle, uint32_t device_handle,
    uint32_t context_dma_handle, uint32_t memory_handle,
    uint32_t subdevice_handle, uint32_t flags,
    uint64_t offset, uint64_t limit,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_context_dma_params_t params = {0};
    nvx_gsp_rmapi_status_t status;
    if (!manager || !object || !client_handle || !device_handle ||
        !context_dma_handle || !memory_handle ||
        client_handle == device_handle || client_handle == context_dma_handle ||
        device_handle == context_dma_handle || !scratch || !response ||
        !timeout_ms || !poll_interval_ms || !delay_ms || offset > limit ||
        !state_valid(object) || object->stage != NVX_GSP_CONTEXT_DMA_IDLE)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    if (object->context_dma_handle != 0U) {
        if (object->client_handle == client_handle &&
            object->device_handle == device_handle &&
            object->context_dma_handle == context_dma_handle &&
            object->params.h_memory == memory_handle &&
            object->params.h_subdevice == subdevice_handle &&
            object->params.flags == flags &&
            object->params.offset == offset && object->params.limit == limit)
            return NVX_GSP_RMAPI_OK;
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    }

    params.h_subdevice = subdevice_handle;
    params.flags = flags;
    params.h_memory = memory_handle;
    params.offset = offset;
    params.limit = limit;
    object->client_handle = client_handle;
    object->device_handle = device_handle;
    object->context_dma_handle = context_dma_handle;
    object->params = params;
    object->completed_status = NVX_GSP_RMAPI_OK;

    status = nvx_gsp_rm_alloc_sync(manager, client_handle, device_handle,
        context_dma_handle, NVX_GSP_RM_CLASS_CONTEXT_DMA, 0U, &params,
        sizeof(params), scratch, scratch_size, response, response_capacity,
        timeout_ms, poll_interval_ms, delay_ms, delay_context,
        &object->operation);
    if (status == NVX_GSP_RMAPI_TIMEOUT) {
        object->stage = NVX_GSP_CONTEXT_DMA_ALLOC_PENDING;
        return status;
    }
    if (status == NVX_GSP_RMAPI_OK) {
        object->stage = NVX_GSP_CONTEXT_DMA_CREATED;
        return status;
    }
    object->client_handle = 0U;
    object->device_handle = 0U;
    object->context_dma_handle = 0U;
    object->params = (nvx_gsp_context_dma_params_t){0};
    return status;
}

nvx_gsp_rmapi_status_t nvx_gsp_context_dma_destroy(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_context_dma_t *object,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    if (!manager || !object || !scratch || !response || !timeout_ms ||
        !poll_interval_ms || !delay_ms || !state_valid(object) ||
        object->stage != NVX_GSP_CONTEXT_DMA_CREATED)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    status = nvx_gsp_rm_free_sync(manager, object->client_handle,
        object->device_handle, object->context_dma_handle, scratch,
        scratch_size, response, response_capacity, timeout_ms,
        poll_interval_ms, delay_ms, delay_context, &object->operation);
    if (status == NVX_GSP_RMAPI_TIMEOUT) {
        object->stage = NVX_GSP_CONTEXT_DMA_FREE_PENDING;
        return status;
    }
    if (status == NVX_GSP_RMAPI_OK) {
        object->stage = NVX_GSP_CONTEXT_DMA_IDLE;
        object->client_handle = 0U;
        object->device_handle = 0U;
        object->context_dma_handle = 0U;
        object->params = (nvx_gsp_context_dma_params_t){0};
    }
    return status;
}

nvx_gsp_rmapi_status_t nvx_gsp_context_dma_recover(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_context_dma_t *object,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    if (!manager || !object || !scratch || !response || !timeout_ms ||
        !poll_interval_ms || !delay_ms || !state_valid(object) ||
        (object->stage != NVX_GSP_CONTEXT_DMA_ALLOC_PENDING &&
         object->stage != NVX_GSP_CONTEXT_DMA_ALLOC_RELEASE_PENDING &&
         object->stage != NVX_GSP_CONTEXT_DMA_FREE_PENDING &&
         object->stage != NVX_GSP_CONTEXT_DMA_FREE_RELEASE_PENDING))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    nvx_gsp_rmapi_status_t status = wait_operation(manager, object, scratch,
        scratch_size, response, response_capacity, timeout_ms,
        poll_interval_ms, delay_ms, delay_context);
    if (status == NVX_GSP_RMAPI_OK &&
        object->stage == NVX_GSP_CONTEXT_DMA_IDLE) {
        object->client_handle = 0U;
        object->device_handle = 0U;
        object->context_dma_handle = 0U;
        object->params = (nvx_gsp_context_dma_params_t){0};
    }
    return status;
}
