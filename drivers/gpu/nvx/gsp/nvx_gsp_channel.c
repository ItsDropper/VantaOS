/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_channel.h"

#include <string.h>

static nvx_gsp_rmapi_status_t free_channel(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_copy_channel_t *channel,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    status = nvx_gsp_rm_free_sync(manager, channel->client_handle,
        channel->device_handle, channel->channel_handle, scratch, scratch_size,
        response, response_capacity, timeout_ms, poll_interval_ms,
        delay_ms, delay_context, &channel->operation);
    if (status == NVX_GSP_RMAPI_TIMEOUT) {
        channel->stage = NVX_GSP_CHANNEL_FREE_PENDING;
        return status;
    }
    if (status == NVX_GSP_RMAPI_OK) {
        channel->allocated = 0U;
        channel->bound = 0U;
    }
    return status;
}

nvx_gsp_rmapi_status_t nvx_gsp_copy_channel_create(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_copy_channel_t *channel,
    uint32_t client_handle, uint32_t device_handle, uint32_t channel_handle,
    const nvx_gsp_channel_alloc_params_t *params,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    if (!manager || !channel || !client_handle || !device_handle ||
        !channel_handle || !params || !scratch || !response ||
        !timeout_ms || !poll_interval_ms || !delay_ms)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    if (channel->stage != NVX_GSP_CHANNEL_IDLE)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    if (channel->client_handle == 0U && channel->device_handle == 0U &&
        channel->channel_handle == 0U && !channel->allocated &&
        !channel->bound && !channel->scheduled) {
        channel->client_handle = client_handle;
        channel->device_handle = device_handle;
        channel->channel_handle = channel_handle;
    } else if (channel->client_handle != client_handle ||
               channel->device_handle != device_handle ||
               channel->channel_handle != channel_handle) {
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    }

    if (!channel->allocated) {
        status = nvx_gsp_rmapi_alloc_ampere_channel_sync(manager,
            client_handle, device_handle, channel_handle, params, scratch,
            scratch_size, response, response_capacity, timeout_ms,
            poll_interval_ms, delay_ms, delay_context, &channel->operation);
        if (status == NVX_GSP_RMAPI_TIMEOUT) {
            channel->stage = NVX_GSP_CHANNEL_ALLOC_PENDING;
            return status;
        }
        if (status != NVX_GSP_RMAPI_OK)
            return status;
        channel->allocated = 1U;
    }

    if (!channel->bound) {
        status = nvx_gsp_rmapi_bind_channel_sync(manager, client_handle,
            channel_handle, NVX_GSP_RM_ENGINE_TYPE_COPY0, scratch, scratch_size,
            response, response_capacity, timeout_ms, poll_interval_ms,
            delay_ms, delay_context, &channel->operation);
        if (status == NVX_GSP_RMAPI_TIMEOUT) {
            channel->stage = NVX_GSP_CHANNEL_BIND_PENDING;
            return status;
        }
        if (status != NVX_GSP_RMAPI_OK) {
            nvx_gsp_rmapi_status_t cleanup = free_channel(manager, channel,
                scratch, scratch_size, response, response_capacity, timeout_ms,
                poll_interval_ms, delay_ms, delay_context);
            return cleanup == NVX_GSP_RMAPI_OK ? status :
                (cleanup == NVX_GSP_RMAPI_TIMEOUT ? cleanup :
                 NVX_GSP_RMAPI_CLEANUP_FAILED);
        }
        channel->bound = 1U;
    }

    if (!channel->scheduled) {
        status = nvx_gsp_rmapi_schedule_channel_sync(manager, client_handle,
            channel_handle, 1, scratch, scratch_size, response,
            response_capacity, timeout_ms, poll_interval_ms, delay_ms,
            delay_context, &channel->operation);
        if (status == NVX_GSP_RMAPI_TIMEOUT) {
            channel->stage = NVX_GSP_CHANNEL_SCHEDULE_PENDING;
            return status;
        }
        if (status != NVX_GSP_RMAPI_OK) {
            nvx_gsp_rmapi_status_t cleanup = free_channel(manager, channel,
                scratch, scratch_size, response, response_capacity, timeout_ms,
                poll_interval_ms, delay_ms, delay_context);
            return cleanup == NVX_GSP_RMAPI_OK ? status :
                (cleanup == NVX_GSP_RMAPI_TIMEOUT ? cleanup :
                 NVX_GSP_RMAPI_CLEANUP_FAILED);
        }
        channel->scheduled = 1U;
    }
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_copy_channel_recover(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_copy_channel_t *channel,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    nvx_gsp_channel_stage_t stage;
    if (!manager || !channel || !scratch || !response || !timeout_ms ||
        !poll_interval_ms || !delay_ms || channel->stage == NVX_GSP_CHANNEL_IDLE ||
        !channel->operation.handle.valid)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    stage = (nvx_gsp_channel_stage_t)channel->stage;
    status = nvx_gsp_rmapi_wait(manager, &channel->operation, scratch,
        scratch_size, response, response_capacity, timeout_ms,
        poll_interval_ms, delay_ms, delay_context);
    if (status == NVX_GSP_RMAPI_TIMEOUT)
        return status;

    if (nvx_gsp_rpc_release(manager, &channel->operation.handle) !=
        NVX_GSP_RPC_OK)
        return NVX_GSP_RMAPI_RPC_ERROR;

    channel->stage = NVX_GSP_CHANNEL_IDLE;
    if (status != NVX_GSP_RMAPI_OK)
        return status;

    switch (stage) {
    case NVX_GSP_CHANNEL_ALLOC_PENDING:
        channel->allocated = 1U;
        break;
    case NVX_GSP_CHANNEL_BIND_PENDING:
        channel->bound = 1U;
        break;
    case NVX_GSP_CHANNEL_SCHEDULE_PENDING:
        channel->scheduled = 1U;
        break;
    case NVX_GSP_CHANNEL_DISABLE_PENDING:
        channel->scheduled = 0U;
        break;
    case NVX_GSP_CHANNEL_FREE_PENDING:
        channel->allocated = 0U;
        channel->bound = 0U;
        break;
    default:
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    }
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_copy_channel_destroy(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_copy_channel_t *channel,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    if (!manager || !channel || !scratch || !response || !timeout_ms ||
        !poll_interval_ms || !delay_ms)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    if (channel->stage != NVX_GSP_CHANNEL_IDLE) {
        status = nvx_gsp_copy_channel_recover(manager, channel, scratch,
            scratch_size, response, response_capacity, timeout_ms,
            poll_interval_ms, delay_ms, delay_context);
        if (status != NVX_GSP_RMAPI_OK)
            return status;
    }

    if (channel->scheduled) {
        status = nvx_gsp_rmapi_schedule_channel_sync(manager,
            channel->client_handle, channel->channel_handle, 0, scratch,
            scratch_size, response, response_capacity, timeout_ms,
            poll_interval_ms, delay_ms, delay_context, &channel->operation);
        if (status == NVX_GSP_RMAPI_TIMEOUT) {
            channel->stage = NVX_GSP_CHANNEL_DISABLE_PENDING;
            return status;
        }
        if (status != NVX_GSP_RMAPI_OK)
            return status;
        channel->scheduled = 0U;
    }

    if (channel->allocated) {
        status = free_channel(manager, channel, scratch, scratch_size,
            response, response_capacity, timeout_ms, poll_interval_ms,
            delay_ms, delay_context);
        if (status != NVX_GSP_RMAPI_OK)
            return status;
    }
    channel->bound = 0U;
    return NVX_GSP_RMAPI_OK;
}
