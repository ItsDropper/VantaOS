/*
 * Copyright (c) 2026 ItsDropper
 * Licensed under the MIT License; see LICENSE.
 */
#ifndef VANTA_NVX_GSP_OS_MEMORY_H
#define VANTA_NVX_GSP_OS_MEMORY_H
#include "nvx_dma.h"
#include "nvx_gsp_rmapi.h"

/* NVIDIA public class/cl0071.h: NV01_MEMORY_SYSTEM_OS_DESCRIPTOR. */
#define NVX_GSP_RM_CLASS_MEMORY_SYSTEM_OS_DESCRIPTOR UINT32_C(0x00000071)
#define NVX_GSP_OS_MEMORY_DESCRIPTOR_OS_PHYS_ADDR UINT32_C(3)
#define NVX_GSP_OS_MEMORY_TYPE_IMAGE UINT32_C(0)
#define NVX_GSP_OS_MEMORY_ATTR_SYSTEM_CONTIGUOUS_CACHED UINT32_C(0x32000000)
#define NVX_GSP_OS_MEMORY_ATTR2_GPU_CACHEABLE_NO UINT32_C(0x00000008)
#define NVX_GSP_OS_MEMORY_DESCRIPTOR_PARAMS_SIZE 40U

/* NVIDIA nvos.h NV_OS_DESC_MEMORY_ALLOCATION_PARAMS, 64-bit ABI. */
typedef struct nvx_gsp_os_memory_params {
    uint32_t type, flags, attr, attr2;
    uint64_t descriptor; /* Physical base itself, not a pointer to a u64. */
    uint64_t limit;       /* Inclusive. */
    uint32_t descriptor_type, tag;
} nvx_gsp_os_memory_params_t;

typedef enum nvx_gsp_os_memory_stage {
    NVX_GSP_OS_MEMORY_IDLE = 0,
    NVX_GSP_OS_MEMORY_ALLOC_PENDING,
    NVX_GSP_OS_MEMORY_ALLOC_RELEASE_PENDING,
    NVX_GSP_OS_MEMORY_CREATED,
    NVX_GSP_OS_MEMORY_FREE_PENDING,
    NVX_GSP_OS_MEMORY_FREE_RELEASE_PENDING,
    NVX_GSP_OS_MEMORY_ALLOC_QUARANTINED,
    NVX_GSP_OS_MEMORY_FREE_QUARANTINED
} nvx_gsp_os_memory_stage_t;

/*
 * Owns the contiguous backing and its RM import. The backing must outlive
 * the RM object. Kernel-privileged RM client required for OS_PHYS_ADDR.
 * Zero-initialize before first use; keep RPC scratch/response buffers stable
 * until each pending operation has been recovered.
 */
typedef struct nvx_gsp_os_memory {
    uint32_t client_handle, device_handle, memory_handle;
    uint64_t requested_size;
    nvx_dma_buffer_t backing;
    nvx_gsp_rmapi_result_t operation;
    nvx_gsp_rmapi_status_t completed_status;
    uint8_t remote_created, stage;
} nvx_gsp_os_memory_t;

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_create(
    nvx_gsp_rpc_manager_t *, nvx_gsp_os_memory_t *,
    uint32_t client, uint32_t device, uint32_t memory,
    size_t size, uint32_t tag,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_recover(
    nvx_gsp_rpc_manager_t *, nvx_gsp_os_memory_t *,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_destroy(
    nvx_gsp_rpc_manager_t *, nvx_gsp_os_memory_t *,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_get_cpu_address(
    const nvx_gsp_os_memory_t *, void **out_address);
nvx_gsp_rmapi_status_t nvx_gsp_os_memory_get_physical_range(
    const nvx_gsp_os_memory_t *, uint64_t *out_base, uint64_t *out_size);

#endif
