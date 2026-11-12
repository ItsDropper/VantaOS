/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_memory.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(nvx_gsp_memory_allocation_params_t) == 128U,
    "NV_MEMORY_ALLOCATION_PARAMS 64-bit ABI size mismatch");
_Static_assert(offsetof(nvx_gsp_memory_allocation_params_t, range_lo) == 48U,
    "NV_MEMORY_ALLOCATION_PARAMS rangeLo offset mismatch");
_Static_assert(offsetof(nvx_gsp_memory_allocation_params_t, size) == 64U,
    "NV_MEMORY_ALLOCATION_PARAMS size offset mismatch");
_Static_assert(offsetof(nvx_gsp_memory_allocation_params_t, limit) == 88U,
    "NV_MEMORY_ALLOCATION_PARAMS limit offset mismatch");
_Static_assert(offsetof(nvx_gsp_memory_allocation_params_t, address) == 96U,
    "NV_MEMORY_ALLOCATION_PARAMS address offset mismatch");
_Static_assert(offsetof(nvx_gsp_memory_allocation_params_t, h_vaspace) == 108U,
    "NV_MEMORY_ALLOCATION_PARAMS hVASpace offset mismatch");

static int ranges_overlap(const void *a, size_t a_size,
                          const void *b, size_t b_size)
{
    uintptr_t ap, bp;
    if (!a || !b || !a_size || !b_size)
        return 0;
    ap = (uintptr_t)a;
    bp = (uintptr_t)b;
    if (a_size > UINTPTR_MAX - ap || b_size > UINTPTR_MAX - bp)
        return 1;
    return ap < bp + b_size && bp < ap + a_size;
}

static int power_of_two_u64(uint64_t value)
{
    return value && (value & (value - 1U)) == 0U;
}

static void clear_owner(nvx_gsp_memory_object_t *object)
{
    object->client_handle = 0U;
    object->parent_handle = 0U;
    object->memory_handle = 0U;
    object->params = (nvx_gsp_memory_allocation_params_t){0};
    object->actual_size = 0U;
    object->limit = 0U;
    object->remote_created = 0U;
    object->result_valid = 0U;
}

static int state_valid(const nvx_gsp_memory_object_t *object)
{
    if (!object || object->stage > NVX_GSP_MEMORY_FREE_RELEASE_PENDING ||
        object->remote_created > 1U || object->result_valid > 1U)
        return 0;
    switch ((nvx_gsp_memory_stage_t)object->stage) {
    case NVX_GSP_MEMORY_IDLE:
        return !object->operation.handle.valid && !object->client_handle &&
            !object->parent_handle && !object->memory_handle &&
            !object->remote_created && !object->result_valid &&
            !object->actual_size && !object->limit;
    case NVX_GSP_MEMORY_ALLOC_PENDING:
        return object->operation.handle.valid &&
            object->operation.function == NVX_GSP_RPC_FN_RM_ALLOC &&
            object->client_handle && object->parent_handle &&
            object->memory_handle && !object->remote_created;
    case NVX_GSP_MEMORY_ALLOC_RELEASE_PENDING:
        return object->operation.handle.valid &&
            object->operation.function == NVX_GSP_RPC_FN_RM_ALLOC &&
            object->client_handle && object->parent_handle &&
            object->memory_handle;
    case NVX_GSP_MEMORY_CREATED:
        return !object->operation.handle.valid && object->client_handle &&
            object->parent_handle && object->memory_handle &&
            object->remote_created;
    case NVX_GSP_MEMORY_FREE_PENDING:
    case NVX_GSP_MEMORY_FREE_RELEASE_PENDING:
        return object->operation.handle.valid &&
            object->operation.function == NVX_GSP_RPC_FN_FREE &&
            object->client_handle && object->parent_handle &&
            object->memory_handle && object->remote_created;
    default:
        return 0;
    }
}

static int decode_allocation_result(
    const void *response, size_t response_size,
    uint64_t requested_size, uint64_t *actual_size, uint64_t *limit)
{
    nvx_gsp_memory_allocation_params_t returned;
    const uint8_t *payload;
    if (!response || !actual_size || !limit ||
        response_size < NVX_GSP_MEMORY_ALLOC_RESPONSE_HEADER_SIZE +
            sizeof(returned))
        return 0;
    payload = (const uint8_t *)response +
        NVX_GSP_MEMORY_ALLOC_RESPONSE_HEADER_SIZE;
    memcpy(&returned, payload, sizeof(returned));
    if (returned.size < requested_size || !returned.size ||
        returned.limit < requested_size - 1U ||
        returned.limit == UINT64_MAX)
        return 0;
    *actual_size = returned.size;
    *limit = returned.limit;
    return 1;
}

static nvx_gsp_rmapi_status_t finish_release(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_memory_object_t *object)
{
    if (nvx_gsp_rpc_release(manager, &object->operation.handle) !=
        NVX_GSP_RPC_OK)
        return NVX_GSP_RMAPI_RPC_ERROR;

    if (object->stage == NVX_GSP_MEMORY_ALLOC_RELEASE_PENDING) {
        if (object->remote_created) {
            object->stage = NVX_GSP_MEMORY_CREATED;
        } else {
            object->stage = NVX_GSP_MEMORY_IDLE;
            clear_owner(object);
        }
    } else if (object->stage == NVX_GSP_MEMORY_FREE_RELEASE_PENDING) {
        if (object->completed_status == NVX_GSP_RMAPI_OK) {
            object->stage = NVX_GSP_MEMORY_IDLE;
            clear_owner(object);
        } else {
            object->stage = NVX_GSP_MEMORY_CREATED;
        }
    } else {
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    }
    return object->completed_status;
}

static nvx_gsp_rmapi_status_t wait_operation(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_memory_object_t *object,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    if (object->stage == NVX_GSP_MEMORY_ALLOC_RELEASE_PENDING ||
        object->stage == NVX_GSP_MEMORY_FREE_RELEASE_PENDING)
        return finish_release(manager, object);

    status = nvx_gsp_rmapi_wait(manager, &object->operation, scratch,
        scratch_size, response, response_capacity, timeout_ms,
        poll_interval_ms, delay_ms, delay_context);
    /* An RPC/transport error leaves the remote outcome uncertain. */
    if (status == NVX_GSP_RMAPI_TIMEOUT || status == NVX_GSP_RMAPI_RPC_ERROR)
        return status;

    object->completed_status = status;
    if (object->stage == NVX_GSP_MEMORY_ALLOC_PENDING &&
        status == NVX_GSP_RMAPI_OK) {
        object->remote_created = 1U;
        object->result_valid = (uint8_t)decode_allocation_result(
            response, object->operation.response_size,
            object->params.size, &object->actual_size, &object->limit);
        if (!object->result_valid)
            object->completed_status = NVX_GSP_RMAPI_MALFORMED_RESPONSE;
    }
    object->stage = object->stage == NVX_GSP_MEMORY_ALLOC_PENDING ?
        NVX_GSP_MEMORY_ALLOC_RELEASE_PENDING :
        NVX_GSP_MEMORY_FREE_RELEASE_PENDING;
    return finish_release(manager, object);
}

nvx_gsp_rmapi_status_t nvx_gsp_memory_object_create(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_memory_object_t *object,
    uint32_t client_handle, uint32_t parent_handle, uint32_t memory_handle,
    const nvx_gsp_memory_allocation_params_t *params,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_memory_allocation_params_t wire;
    nvx_gsp_rmapi_status_t status;
    if (!manager || !object || !params || !client_handle || !parent_handle ||
        !memory_handle || !scratch || !response || !scratch_size || !timeout_ms ||
        !poll_interval_ms || !delay_ms || !params->size ||
        !power_of_two_u64(params->alignment) ||
        params->alignment < NVX_GSP_MEMORY_PAGE_SIZE ||
        params->address || params->limit || params->h_vaspace ||
        params->offset || params->internal_flags ||
        ((params->range_lo || params->range_hi) &&
         params->range_lo > params->range_hi) ||
        response_capacity < NVX_GSP_MEMORY_ALLOC_RESPONSE_HEADER_SIZE +
            sizeof(*params) ||
        ranges_overlap(manager, sizeof(*manager), object, sizeof(*object)) ||
        ranges_overlap(object, sizeof(*object), params, sizeof(*params)) ||
        ranges_overlap(object, sizeof(*object), scratch, scratch_size) ||
        ranges_overlap(object, sizeof(*object), response, response_capacity) ||
        ranges_overlap(params, sizeof(*params), scratch, scratch_size) ||
        ranges_overlap(params, sizeof(*params), response, response_capacity) ||
        ranges_overlap(scratch, scratch_size, response, response_capacity) ||
        !state_valid(object) || object->stage != NVX_GSP_MEMORY_IDLE)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    wire = *params;
    /* These are RM outputs, never caller-provided input values. */
    wire.pitch = params->pitch;
    wire.format = params->format;
    wire.compr_covg = params->compr_covg;
    wire.zcull_covg = params->zcull_covg;
    wire.limit = 0U;
    wire.address = 0U;
    object->client_handle = client_handle;
    object->parent_handle = parent_handle;
    object->memory_handle = memory_handle;
    object->params = wire;
    object->actual_size = 0U;
    object->limit = 0U;
    object->remote_created = 0U;
    object->result_valid = 0U;
    object->completed_status = NVX_GSP_RMAPI_OK;

    status = nvx_gsp_rm_alloc_submit(manager, client_handle, parent_handle,
        memory_handle, NVX_GSP_RM_CLASS_MEMORY_SYSTEM, 0U, &wire,
        sizeof(wire), scratch, scratch_size, response, response_capacity,
        &object->operation);
    if (status != NVX_GSP_RMAPI_OK && !object->operation.handle.valid) {
        clear_owner(object);
        return status;
    }
    object->stage = NVX_GSP_MEMORY_ALLOC_PENDING;
    if (status != NVX_GSP_RMAPI_OK)
        return status;
    return wait_operation(manager, object, scratch, scratch_size, response,
        response_capacity, timeout_ms, poll_interval_ms, delay_ms,
        delay_context);
}

nvx_gsp_rmapi_status_t nvx_gsp_memory_object_destroy(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_memory_object_t *object,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    if (!manager || !object || !scratch || !response || !scratch_size || !timeout_ms ||
        !poll_interval_ms || !delay_ms ||
        ranges_overlap(object, sizeof(*object), scratch, scratch_size) ||
        ranges_overlap(object, sizeof(*object), response, response_capacity) ||
        ranges_overlap(scratch, scratch_size, response, response_capacity) ||
        !state_valid(object) || object->stage != NVX_GSP_MEMORY_CREATED)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    status = nvx_gsp_rm_free_submit(manager, object->client_handle,
        object->parent_handle, object->memory_handle, scratch, scratch_size,
        response, response_capacity, &object->operation);
    if (status != NVX_GSP_RMAPI_OK && !object->operation.handle.valid)
        return status;
    object->stage = NVX_GSP_MEMORY_FREE_PENDING;
    if (status != NVX_GSP_RMAPI_OK)
        return status;
    return wait_operation(manager, object, scratch, scratch_size, response,
        response_capacity, timeout_ms, poll_interval_ms, delay_ms,
        delay_context);
}

nvx_gsp_rmapi_status_t nvx_gsp_memory_object_recover(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_memory_object_t *object,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    if (!manager || !object || !scratch || !response || !timeout_ms ||
        !poll_interval_ms || !delay_ms ||
        ranges_overlap(object, sizeof(*object), scratch, scratch_size) ||
        ranges_overlap(object, sizeof(*object), response, response_capacity) ||
        ranges_overlap(scratch, scratch_size, response, response_capacity) ||
        !state_valid(object) ||
        (object->stage != NVX_GSP_MEMORY_ALLOC_PENDING &&
         object->stage != NVX_GSP_MEMORY_ALLOC_RELEASE_PENDING &&
         object->stage != NVX_GSP_MEMORY_FREE_PENDING &&
         object->stage != NVX_GSP_MEMORY_FREE_RELEASE_PENDING))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    return wait_operation(manager, object, scratch, scratch_size, response,
        response_capacity, timeout_ms, poll_interval_ms, delay_ms,
        delay_context);
}
