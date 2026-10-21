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
#define NVX_GSP_RPC_FN_MAP_MEMORY_DMA 14U
#define NVX_GSP_RPC_FN_UNMAP_MEMORY_DMA 15U
#define NVX_GSP_RPC_FN_RM_ALLOC      103U
#define NVX_GSP_RMAPI_MAX_PARAMS     2048U

typedef enum nvx_gsp_rmapi_status {
    NVX_GSP_RMAPI_OK = 0,
    NVX_GSP_RMAPI_INVALID_ARGUMENT,
    NVX_GSP_RMAPI_NO_SPACE,
    NVX_GSP_RMAPI_RPC_ERROR,
    NVX_GSP_RMAPI_RESPONSE_TOO_SMALL,
    NVX_GSP_RMAPI_MALFORMED_RESPONSE,
    NVX_GSP_RMAPI_REMOTE_ERROR,
    NVX_GSP_RMAPI_TIMEOUT
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

typedef struct nvx_gsp_map_memory_dma {
    uint32_t client, device, dma, memory;
    uint64_t offset, length;
    uint32_t flags, flags2, kind_override;
    uint64_t dma_offset;
} nvx_gsp_map_memory_dma_t;

typedef struct nvx_gsp_unmap_memory_dma {
    uint32_t client, device, dma, memory;
    uint32_t flags;
    uint64_t dma_offset, size;
} nvx_gsp_unmap_memory_dma_t;

typedef struct nvx_gsp_rmapi_result {
    nvx_gsp_rpc_handle_t handle;
    uint32_t function;
    size_t response_size;
} nvx_gsp_rmapi_result_t;

/* NVOS46/NVOS47-compatible memory mapping RPCs (function IDs 14/15). */
nvx_gsp_rmapi_status_t nvx_gsp_rm_map_memory_dma_submit(
    nvx_gsp_rpc_manager_t *manager, const nvx_gsp_map_memory_dma_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, nvx_gsp_rmapi_result_t *out);
nvx_gsp_rmapi_status_t nvx_gsp_rm_unmap_memory_dma_submit(
    nvx_gsp_rpc_manager_t *manager, const nvx_gsp_unmap_memory_dma_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, nvx_gsp_rmapi_result_t *out);
nvx_gsp_rmapi_status_t nvx_gsp_rm_map_memory_dma_sync(
    nvx_gsp_rpc_manager_t *manager, const nvx_gsp_map_memory_dma_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context,
    uint64_t *out_dma_offset, nvx_gsp_rmapi_result_t *out_operation);
nvx_gsp_rmapi_status_t nvx_gsp_rm_unmap_memory_dma_sync(
    nvx_gsp_rpc_manager_t *manager, const nvx_gsp_unmap_memory_dma_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context,
    nvx_gsp_rmapi_result_t *out_operation);
nvx_gsp_rmapi_status_t nvx_gsp_rm_map_memory_dma_result(
    const nvx_gsp_rmapi_result_t *operation, const void *response,
    size_t response_size, uint64_t *out_dma_offset);

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
    void *response, size_t response_capacity, nvx_gsp_rmapi_result_t *out);

typedef void (*nvx_gsp_rmapi_delay_fn)(uint32_t ms, void *context);

/*
 * Pump and wait for a submitted operation. Timeout does not cancel an in-flight
 * firmware RPC; retain the operation handle and recover/quiesce before reuse.
 */
nvx_gsp_rmapi_status_t nvx_gsp_rmapi_wait(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_rmapi_result_t *operation,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

/* Synchronous wrappers; timeout leaves the in-flight handle valid in out. */
nvx_gsp_rmapi_status_t nvx_gsp_rm_alloc_sync(
    nvx_gsp_rpc_manager_t *manager, uint32_t client, uint32_t parent,
    uint32_t object, uint32_t class_id, uint32_t flags,
    const void *params, size_t params_size, void *scratch, size_t scratch_size,
    void *response, size_t response_capacity, uint32_t timeout_ms,
    uint32_t poll_interval_ms, nvx_gsp_rmapi_delay_fn delay_ms,
    void *delay_context, nvx_gsp_rmapi_result_t *out);
nvx_gsp_rmapi_status_t nvx_gsp_rm_control_sync(
    nvx_gsp_rpc_manager_t *manager, uint32_t client, uint32_t object,
    uint32_t command, uint32_t rmapi_rpc_flags, uint32_t rmctrl_flags,
    uint32_t access_right, const void *params, size_t params_size,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context,
    nvx_gsp_rmapi_result_t *out);
nvx_gsp_rmapi_status_t nvx_gsp_rm_free_sync(
    nvx_gsp_rpc_manager_t *manager, uint32_t root, uint32_t parent,
    uint32_t object, void *scratch, size_t scratch_size,
    void *response, size_t response_capacity, uint32_t timeout_ms,
    uint32_t poll_interval_ms, nvx_gsp_rmapi_delay_fn delay_ms,
    void *delay_context, nvx_gsp_rmapi_result_t *out);

/* Public NVA06F GPFIFO channel schedule control command (R535 SDK ABI). */
#define NVX_GSP_RM_CTRL_GPFIFO_SCHEDULE UINT32_C(0xA06F0103)
nvx_gsp_rmapi_status_t nvx_gsp_rmapi_schedule_channel_sync(
    nvx_gsp_rpc_manager_t *manager, uint32_t client, uint32_t channel,
    int enable, void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context,
    nvx_gsp_rmapi_result_t *out);

/* Public NVIDIA class IDs for the root, device and virtual-address-space objects. */
#define NVX_GSP_RM_CLASS_ROOT             0x00000000U
#define NVX_GSP_RM_CLASS_DEVICE_0         0x00000080U
#define NVX_GSP_RM_CLASS_MEMORY_VIRTUAL   0x00000070U

typedef struct nvx_gsp_rm_core_objects {
    uint32_t client_handle;
    uint32_t device_handle;
    uint32_t vaspace_handle;
    uint8_t client_created;
    uint8_t device_created;
    uint8_t vaspace_created;
    uint8_t in_flight_stage; /* 1=root, 2=device, 3=VA space; nonzero means outcome uncertain. */
} nvx_gsp_rm_core_objects_t;

/*
 * Allocate RM root/client, device, and NV01_MEMORY_VIRTUAL objects in order.
 * The caller supplies stable, unique RM handles and an explicit device VA
 * size/limit. On failure, completed allocations are freed in reverse order;
 * a timeout is returned without freeing potentially in-flight objects.
 */
nvx_gsp_rmapi_status_t nvx_gsp_rm_create_core_objects(
    nvx_gsp_rpc_manager_t *manager, uint32_t client_handle,
    uint32_t device_handle, uint32_t vaspace_handle,
    uint32_t device_flags, uint64_t device_va_space_size,
    uint64_t vaspace_limit, void *scratch, size_t scratch_size,
    void *response, size_t response_capacity, uint32_t timeout_ms,
    uint32_t poll_interval_ms, nvx_gsp_rmapi_delay_fn delay_ms,
    void *delay_context, nvx_gsp_rm_core_objects_t *out_objects);

/* Validate and extract the RM-side status from a completed response payload. */
nvx_gsp_rmapi_status_t nvx_gsp_rmapi_decode_status(
    const nvx_gsp_rmapi_result_t *operation,
    const void *response, size_t response_size, uint32_t *out_status);

#endif
