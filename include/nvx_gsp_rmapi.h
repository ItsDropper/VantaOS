/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_RMAPI_H
#define VANTA_NVX_GSP_RMAPI_H

#include <stddef.h>
#include <stdint.h>
#include "nvx_gsp_rpc.h"

/* Function numbers from NVIDIA's public rpc_global_enums.h (R535 ABI). */
#define NVX_GSP_RPC_FN_FREE          10U
#define NVX_GSP_RPC_FN_RM_CONTROL    76U
#define NVX_GSP_RPC_FN_RM_ALLOC      103U
#define NVX_GSP_RMAPI_MAX_PARAMS     2048U

typedef enum nvx_gsp_rmapi_status {
    NVX_GSP_RMAPI_OK = 0,
    NVX_GSP_RMAPI_INVALID_ARGUMENT,
    NVX_GSP_RMAPI_NO_SPACE,
    NVX_GSP_RMAPI_RPC_ERROR,
    NVX_GSP_RMAPI_RESPONSE_TOO_SMALL,
    NVX_GSP_RMAPI_MALFORMED_RESPONSE,
    NVX_GSP_RMAPI_REMOTE_ERROR
} nvx_gsp_rmapi_status_t;

/*
 * Exact R535 GSP RM RPC payload shapes. Flexible parameter bytes are copied
 * verbatim: callers must use the matching NVIDIA class/control ABI and may not
 * pass native pointers. Handles are opaque RM handles, not kernel pointers.
 */
typedef struct nvx_gsp_rm_alloc_request {
    uint32_t h_client;
    uint32_t h_parent;
    uint32_t h_object;
    uint32_t h_class;
    uint32_t status;
    uint32_t params_size;
    uint32_t flags;
    uint8_t reserved[4];
    uint8_t params[];
} nvx_gsp_rm_alloc_request_t;

typedef struct nvx_gsp_rm_control_request {
    uint32_t h_client;
    uint32_t h_object;
    uint32_t command;
    uint32_t status;
    uint32_t params_size;
    uint32_t rmapi_rpc_flags;
    uint32_t rmctrl_flags;
    uint32_t rmctrl_access_right;
    uint64_t reserved0;
    uint8_t params[];
} nvx_gsp_rm_control_request_t;

typedef struct nvx_gsp_rm_free_request {
    uint32_t h_root;
    uint32_t h_object_parent;
    uint32_t h_object_old;
    int32_t status;
} nvx_gsp_rm_free_request_t;

typedef struct nvx_gsp_rmapi_result {
    nvx_gsp_rpc_handle_t handle;
    uint32_t function;
    size_t response_size;
} nvx_gsp_rmapi_result_t;

/* Submit asynchronous operations. Keep response storage alive until queried. */
nvx_gsp_rmapi_status_t nvx_gsp_rm_alloc_submit(
    nvx_gsp_rpc_manager_t *manager, uint32_t client, uint32_t parent,
    uint32_t object, uint32_t class_id, uint32_t flags,
    const void *params, size_t params_size, void *scratch, size_t scratch_size,
    void *response, size_t response_capacity, nvx_gsp_rmapi_result_t *out);
nvx_gsp_rmapi_status_t nvx_gsp_rm_control_submit(
    nvx_gsp_rpc_manager_t *manager, uint32_t client, uint32_t object,
    uint32_t command, uint32_t rmapi_rpc_flags, uint32_t rmctrl_flags,
    uint32_t access_right, const void *params, size_t params_size,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, nvx_gsp_rmapi_result_t *out);
nvx_gsp_rmapi_status_t nvx_gsp_rm_free_submit(
    nvx_gsp_rpc_manager_t *manager, uint32_t root, uint32_t parent,
    uint32_t object, void *scratch, size_t scratch_size,
    nvx_gsp_rmapi_result_t *out);

/* Validate and extract the RM-side status from a completed response payload. */
nvx_gsp_rmapi_status_t nvx_gsp_rmapi_decode_status(
    const nvx_gsp_rmapi_result_t *operation,
    const void *response, size_t response_size, uint32_t *out_status);

#endif
