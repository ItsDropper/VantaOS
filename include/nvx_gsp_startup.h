/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_STARTUP_H
#define VANTA_NVX_GSP_STARTUP_H

#include "nvx_gsp_gmcapi.h"
#include "nvx_gsp_msgq_ga10x.h"
#include "nvx_gsp_secure_boot.h"
#include "nvx_gsp_rpc.h"
#include "nvx_gsp_system_info.h"

typedef enum nvx_gsp_startup_status {
    NVX_GSP_STARTUP_OK = 0,
    NVX_GSP_STARTUP_INVALID_ARGUMENT,
    NVX_GSP_STARTUP_QUEUE_PREPARE_FAILED,
    NVX_GSP_STARTUP_BOOTSTRAP_FAILED,
    NVX_GSP_STARTUP_STATUS_LINK_FAILED,
    NVX_GSP_STARTUP_GMCAPI_INIT_FAILED,
    NVX_GSP_STARTUP_GSP_INIT_FAILED,
    NVX_GSP_STARTUP_RPC_INIT_FAILED
} nvx_gsp_startup_status_t;

typedef struct nvx_gsp_startup_result {
    nvx_gsp_startup_status_t status;
    nvx_gsp_msgq_ga10x_status_t queue_status;
    nvx_gsp_gmcapi_status_t gmcapi_status;
    nvx_gsp_falcon_status_t bootstrap_status;
    nvx_gsp_fwsec_result_snapshot_t frts_result;
    nvx_gsp_boot_trace_t boot_trace;
    uint32_t booter_mailbox1;
    uint32_t gsp_init_remote_status;
    size_t static_info_size;
    uint8_t gsp_rm_initialized;
    uint8_t rpc_initialized;
} nvx_gsp_startup_result_t;

typedef struct nvx_gsp_startup_platform_info {
    const struct pci_device *pci_device;
    const nvx_gsp_system_info_t *trusted_gpu_facts;
    void *encoded_system_info_buffer;
    size_t encoded_system_info_capacity;
} nvx_gsp_startup_platform_info_t;

/*
 * PCI-backed entry point. It collects actual PCI identity/host facts, merges
 * only explicitly supplied GPU/platform facts, encodes NVKV into caller-owned
 * storage, then invokes the standard GA10x startup transaction. All memory
 * objects and DMA buffers must be disjoint from encoded_system_info_buffer.
 */
nvx_gsp_startup_status_t nvx_gsp_ga10x_start_and_init_rm_from_pci(
    const nvx_gsp_startup_platform_info_t *platform,
    nvx_dma_buffer_t *staged_vbios, nvx_dma_sync_t *staged_vbios_sync,
    const nvx_gsp_vbios_fwsec_info_t *fwsec_info, uint32_t fuse_version,
    nvx_gsp_boot_memory_t *boot_memory,
    nvx_dma_buffer_t *init_args, nvx_dma_sync_t *init_args_sync,
    nvx_dma_buffer_t *shared_queue, nvx_dma_sync_t *shared_queue_sync,
    const nvx_gsp_modern_shared_queue_layout_t *queue_layout,
    const nvx_mmio_region_t *bar0, uint64_t frts_framebuffer_offset,
    void *static_info, size_t static_info_capacity, size_t *out_static_info_size,
    void *scratch, size_t scratch_size,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    nvx_gsp_msgq_ga10x_t *queue_runtime,
    nvx_gsp_gmcapi_transport_t *gmcapi,
    nvx_gsp_msgq_rpc_transport_t *rpc_transport,
    nvx_gsp_rpc_manager_t *rpc_manager,
    nvx_gsp_startup_result_t *out_result);

/*
 * Zero-initialize queue_runtime, gmcapi, rpc_transport and rpc_manager before first use.
 *
 * End-to-end GA10x startup transaction:
 *   prepare CPU command queue -> FWSEC-FRTS -> RISC-V/SEC2 Booter Load ->
 *   wait for RISC-V active -> link GSP status queue -> GMCAPI GSP_INIT.
 *
 * encoded_system_info must be produced by a firmware-compatible OS system-info
 * encoder. This function deliberately refuses to invent system topology or
 * registry data. On failure after GSP starts, DMA remains shared/device-owned
 * until the caller proves the engine quiescent and performs recovery.
 */
nvx_gsp_startup_status_t nvx_gsp_ga10x_start_and_init_rm(
    nvx_dma_buffer_t *staged_vbios, nvx_dma_sync_t *staged_vbios_sync,
    const nvx_gsp_vbios_fwsec_info_t *fwsec_info, uint32_t fuse_version,
    nvx_gsp_boot_memory_t *boot_memory,
    nvx_dma_buffer_t *init_args, nvx_dma_sync_t *init_args_sync,
    nvx_dma_buffer_t *shared_queue, nvx_dma_sync_t *shared_queue_sync,
    const nvx_gsp_modern_shared_queue_layout_t *queue_layout,
    const nvx_mmio_region_t *bar0, uint64_t frts_framebuffer_offset,
    const void *encoded_system_info, size_t encoded_system_info_size,
    void *static_info, size_t static_info_capacity, size_t *out_static_info_size,
    void *scratch, size_t scratch_size,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    nvx_gsp_msgq_ga10x_t *queue_runtime,
    nvx_gsp_gmcapi_transport_t *gmcapi,
    nvx_gsp_msgq_rpc_transport_t *rpc_transport,
    nvx_gsp_rpc_manager_t *rpc_manager,
    nvx_gsp_startup_result_t *out_result);

#endif
