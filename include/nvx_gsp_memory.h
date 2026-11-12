/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_MEMORY_H
#define VANTA_NVX_GSP_MEMORY_H

#include "nvx_gsp_rmapi.h"

/* NVIDIA public class/cl003e.h: NV01_MEMORY_SYSTEM. */
#define NVX_GSP_RM_CLASS_MEMORY_SYSTEM UINT32_C(0x0000003E)
#define NVX_GSP_MEMORY_PAGE_SIZE UINT64_C(4096)
#define NVX_GSP_MEMORY_TYPE_IMAGE UINT32_C(0)
#define NVX_GSP_MEMORY_ALLOC_RESPONSE_HEADER_SIZE 32U

/*
 * NVIDIA public nvos.h: NV_MEMORY_ALLOCATION_PARAMS (64-bit ABI).
 * Flags/attributes retain NVIDIA's NVOS32 encoding; callers must select
 * supported values for the target RM/firmware release.
 */
typedef struct nvx_gsp_memory_allocation_params {
    uint32_t owner;
    uint32_t type;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    int32_t pitch;
    uint32_t attr;
    uint32_t attr2;
    uint32_t format;
    uint32_t compr_covg;
    uint32_t zcull_covg;
    uint64_t range_lo;
    uint64_t range_hi;
    uint64_t size;
    uint64_t alignment;
    uint64_t offset;
    uint64_t limit;
    uint64_t address;
    uint32_t ctag_offset;
    uint32_t h_vaspace;
    uint32_t internal_flags;
    uint32_t tag;
    int32_t numa_node;
} nvx_gsp_memory_allocation_params_t;

typedef enum nvx_gsp_memory_stage {
    NVX_GSP_MEMORY_IDLE = 0,
    NVX_GSP_MEMORY_ALLOC_PENDING,
    NVX_GSP_MEMORY_ALLOC_RELEASE_PENDING,
    NVX_GSP_MEMORY_CREATED,
    NVX_GSP_MEMORY_FREE_PENDING,
    NVX_GSP_MEMORY_FREE_RELEASE_PENDING
} nvx_gsp_memory_stage_t;

typedef struct nvx_gsp_memory_object {
    uint32_t client_handle;
    uint32_t parent_handle;
    uint32_t memory_handle;
    nvx_gsp_memory_allocation_params_t params;
    uint64_t actual_size;
    uint64_t limit;
    nvx_gsp_rmapi_result_t operation;
    nvx_gsp_rmapi_status_t completed_status;
    uint8_t remote_created;
    uint8_t result_valid;
    uint8_t stage;
} nvx_gsp_memory_object_t;

/*
 * Allocate a real RM system-memory object. params->size is the requested
 * byte count and params->alignment must be a power of two >= 4 KiB.
 *
 * This API deliberately does not invent NVOS32 placement/cache flags. The
 * caller supplies the version-matched NVIDIA attr/attr2/flags values. The
 * returned size/limit are accepted only when the complete RM_ALLOC response
 * contains the updated NV_MEMORY_ALLOCATION_PARAMS payload.
 *
 * Zero-initialize object state. Keep response storage alive until completion
 * or recovery. A malformed/missing allocation result still retains ownership
 * of a remotely-created object so it can be freed safely.
 */
nvx_gsp_rmapi_status_t nvx_gsp_memory_object_create(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_memory_object_t *object,
    uint32_t client_handle, uint32_t parent_handle, uint32_t memory_handle,
    const nvx_gsp_memory_allocation_params_t *params,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

nvx_gsp_rmapi_status_t nvx_gsp_memory_object_destroy(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_memory_object_t *object,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

nvx_gsp_rmapi_status_t nvx_gsp_memory_object_recover(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_memory_object_t *object,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

#endif
