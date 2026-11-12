/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_channel_runtime.h"

#include <string.h>

static nvx_gsp_rmapi_status_t bind_fifo(nvx_gsp_channel_runtime_t *runtime)
{
    nvx_gsp_userd_status_t status = nvx_gsp_userd_channel_init(
        runtime->userd_memory, runtime->userd_offset, runtime->userd_bytes,
        &runtime->fifo, runtime->entries, runtime->entries_mapping_bytes,
        runtime->doorbell, runtime->doorbell_token, runtime->entry_count,
        &runtime->userd);
    if (status != NVX_GSP_USERD_OK)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    runtime->stage = NVX_GSP_CHANNEL_RUNTIME_READY;
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_channel_runtime_create(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_channel_runtime_t *runtime,
    uint32_t client, uint32_t device, uint32_t channel, uint32_t vaspace,
    uint32_t channel_id, uint32_t subdevice_mask, uint64_t ring_gpu_va,
    uint32_t entry_count, nvx_gsp_os_memory_t *userd_memory,
    uint64_t userd_offset, size_t userd_bytes, volatile uint32_t *entries,
    size_t entries_bytes, volatile uint32_t *doorbell, uint32_t doorbell_token,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_userd_status_t userd_status;
    nvx_gsp_rmapi_status_t status;

    if (!manager || !runtime || runtime->stage != NVX_GSP_CHANNEL_RUNTIME_IDLE ||
        runtime->rm.stage != NVX_GSP_CHANNEL_IDLE || !userd_memory ||
        !entries || !doorbell || !scratch || !response || !timeout_ms ||
        !poll_ms || !delay_ms)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    memset(runtime, 0, sizeof(*runtime));
    userd_status = nvx_gsp_userd_view_init(userd_memory, userd_offset,
        userd_bytes, &runtime->userd);
    if (userd_status != NVX_GSP_USERD_OK)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    userd_status = nvx_gsp_channel_params_init_ampere(&runtime->params,
        client, device, vaspace, channel_id, NVX_GSP_RM_ENGINE_TYPE_COPY0,
        subdevice_mask, ring_gpu_va, entry_count, userd_memory, userd_offset);
    if (userd_status != NVX_GSP_USERD_OK)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    status = nvx_gsp_os_memory_acquire(userd_memory);
    if (status != NVX_GSP_RMAPI_OK)
        return status;
    runtime->userd_reference_held = 1U;
    runtime->userd_memory = userd_memory;
    runtime->entries = entries;
    runtime->entries_mapping_bytes = entries_bytes;
    runtime->doorbell = doorbell;
    runtime->doorbell_token = doorbell_token;
    runtime->entry_count = entry_count;
    runtime->userd_offset = userd_offset;
    runtime->userd_bytes = userd_bytes;
    runtime->stage = NVX_GSP_CHANNEL_RUNTIME_CREATE_PENDING;

    status = nvx_gsp_copy_channel_create(manager, &runtime->rm, client,
        device, channel, &runtime->params, scratch, scratch_size, response,
        response_size, timeout_ms, poll_ms, delay_ms, delay_context);
    if (status != NVX_GSP_RMAPI_OK) {
        if (runtime->rm.stage == NVX_GSP_CHANNEL_IDLE &&
            !runtime->rm.allocated) {
            nvx_gsp_rmapi_status_t release_status =
                nvx_gsp_os_memory_release(userd_memory);
            if (release_status != NVX_GSP_RMAPI_OK)
                return NVX_GSP_RMAPI_CLEANUP_FAILED;
            memset(runtime, 0, sizeof(*runtime));
        }
        return status;
    }
    return bind_fifo(runtime);
}

nvx_gsp_rmapi_status_t nvx_gsp_channel_runtime_recover(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_channel_runtime_t *runtime,
    void *scratch, size_t scratch_size, void *response,
    size_t response_size, uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;

    if (!manager || !runtime || !scratch || !response || !timeout_ms ||
        !poll_ms || !delay_ms ||
        (runtime->stage != NVX_GSP_CHANNEL_RUNTIME_CREATE_PENDING &&
         runtime->stage != NVX_GSP_CHANNEL_RUNTIME_DESTROY_PENDING))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    if (runtime->stage == NVX_GSP_CHANNEL_RUNTIME_DESTROY_PENDING)
        return nvx_gsp_channel_runtime_stop(manager, runtime, NULL, scratch,
            scratch_size, response, response_size, timeout_ms, poll_ms,
            delay_ms, delay_context);

    if (runtime->rm.stage != NVX_GSP_CHANNEL_IDLE) {
        status = nvx_gsp_copy_channel_recover(manager, &runtime->rm,
            scratch, scratch_size, response, response_size, timeout_ms,
            poll_ms, delay_ms, delay_context);
        if (status != NVX_GSP_RMAPI_OK)
            return status;
    }

    if (!runtime->rm.allocated &&
        runtime->rm.stage == NVX_GSP_CHANNEL_IDLE) {
        if (runtime->userd_reference_held) {
            status = nvx_gsp_os_memory_release(runtime->userd_memory);
            if (status != NVX_GSP_RMAPI_OK)
                return NVX_GSP_RMAPI_CLEANUP_FAILED;
        }
        memset(runtime, 0, sizeof(*runtime));
        return NVX_GSP_RMAPI_OK;
    }

    /* A recovered ALLOC/BIND/SCHEDULE RPC is only one stage of creation. */
    status = nvx_gsp_copy_channel_create(manager, &runtime->rm,
        runtime->rm.client_handle, runtime->rm.device_handle,
        runtime->rm.channel_handle, &runtime->params, scratch, scratch_size,
        response, response_size, timeout_ms, poll_ms, delay_ms, delay_context);
    if (status != NVX_GSP_RMAPI_OK)
        return status;
    return bind_fifo(runtime);
}

nvx_gsp_rmapi_status_t nvx_gsp_channel_runtime_stop(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_channel_runtime_t *runtime,
    const nvx_gsp_ce_workload_t *workload, void *scratch,
    size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;

    if (!manager || !runtime || !scratch || !response || !timeout_ms ||
        !poll_ms || !delay_ms ||
        (runtime->stage != NVX_GSP_CHANNEL_RUNTIME_READY &&
         runtime->stage != NVX_GSP_CHANNEL_RUNTIME_CREATE_PENDING &&
         runtime->stage != NVX_GSP_CHANNEL_RUNTIME_DESTROY_PENDING))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    if (workload && (workload->stage != NVX_GSP_CE_WORKLOAD_IDLE ||
                     workload->pinned_count != 0U))
        return NVX_GSP_RMAPI_BUSY;

    if (runtime->stage == NVX_GSP_CHANNEL_RUNTIME_CREATE_PENDING) {
        status = nvx_gsp_channel_runtime_recover(manager, runtime, scratch,
            scratch_size, response, response_size, timeout_ms, poll_ms,
            delay_ms, delay_context);
        if (status != NVX_GSP_RMAPI_OK)
            return status;
        if (runtime->stage == NVX_GSP_CHANNEL_RUNTIME_IDLE)
            return NVX_GSP_RMAPI_OK;
    }

    runtime->stage = NVX_GSP_CHANNEL_RUNTIME_DESTROY_PENDING;
    status = nvx_gsp_copy_channel_destroy(manager, &runtime->rm, scratch,
        scratch_size, response, response_size, timeout_ms, poll_ms,
        delay_ms, delay_context);
    if (status != NVX_GSP_RMAPI_OK)
        return status;

    if (runtime->userd_reference_held) {
        status = nvx_gsp_os_memory_release(runtime->userd_memory);
        if (status != NVX_GSP_RMAPI_OK)
            return NVX_GSP_RMAPI_CLEANUP_FAILED;
    }
    memset(runtime, 0, sizeof(*runtime));
    return NVX_GSP_RMAPI_OK;
}
