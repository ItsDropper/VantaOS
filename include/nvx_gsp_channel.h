/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_CHANNEL_H
#define VANTA_NVX_GSP_CHANNEL_H

#include "nvx_gsp_rmapi.h"

typedef enum nvx_gsp_channel_stage {
    NVX_GSP_CHANNEL_IDLE = 0,
    NVX_GSP_CHANNEL_ALLOC_PENDING,
    NVX_GSP_CHANNEL_BIND_PENDING,
    NVX_GSP_CHANNEL_SCHEDULE_PENDING,
    NVX_GSP_CHANNEL_DISABLE_PENDING,
    NVX_GSP_CHANNEL_FREE_PENDING
} nvx_gsp_channel_stage_t;

typedef struct nvx_gsp_copy_channel {
    uint32_t client_handle;
    uint32_t device_handle;
    uint32_t channel_handle;
    uint8_t allocated;
    uint8_t bound;
    uint8_t scheduled;
    uint8_t stage;
    nvx_gsp_rmapi_result_t operation;
} nvx_gsp_copy_channel_t;

/*
 * Zero-initialize the state before first use. Repeated create calls resume
 * completed stages after recover(); the same handles and allocation params
 * must be supplied. A timeout is not cancellation: keep the RPC response
 * storage alive and call recover() before reusing it or tearing down.
 */
nvx_gsp_rmapi_status_t nvx_gsp_copy_channel_create(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_copy_channel_t *channel,
    uint32_t client_handle, uint32_t device_handle, uint32_t channel_handle,
    const nvx_gsp_channel_alloc_params_t *params,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

/* Complete a timed-out create/destroy stage; timeout preserves its handle. */
nvx_gsp_rmapi_status_t nvx_gsp_copy_channel_recover(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_copy_channel_t *channel,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

/* Disable scheduling before freeing; stops at the first uncertain/failing stage. */
nvx_gsp_rmapi_status_t nvx_gsp_copy_channel_destroy(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_copy_channel_t *channel,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

#endif
