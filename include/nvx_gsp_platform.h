/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_PLATFORM_H
#define VANTA_NVX_GSP_PLATFORM_H

#include "nvx_attach.h"
#include "nvx_gsp_boot_memory.h"
#include "nvx_gsp_ga10x.h"
#include "nvx_gsp_rpc.h"
#include "nvx_gsp_startup.h"
#include "nvx_gsp_wpr_policy.h"

#define NVX_GSP_PLATFORM_SYSTEM_INFO_CAPACITY 4096U
#define NVX_GSP_PLATFORM_WORK_CAPACITY (NVX_GSP_MAX_MESSAGE_PAGES * NVX_GSP_PAGE_SIZE)

typedef enum nvx_gsp_platform_status {
    NVX_GSP_PLATFORM_OK = 0,
    NVX_GSP_PLATFORM_INVALID_ARGUMENT,
    NVX_GSP_PLATFORM_UNSUPPORTED_DEVICE,
    NVX_GSP_PLATFORM_BAR_INVALID,
    NVX_GSP_PLATFORM_ATTACH_FAILED,
    NVX_GSP_PLATFORM_VBIOS_FAILED,
    NVX_GSP_PLATFORM_FIRMWARE_FAILED,
    NVX_GSP_PLATFORM_BOOT_MEMORY_FAILED,
    NVX_GSP_PLATFORM_FUSE_FAILED,
    NVX_GSP_PLATFORM_WPR_FAILED,
    NVX_GSP_PLATFORM_QUEUE_FAILED,
    NVX_GSP_PLATFORM_INIT_ARGS_FAILED,
    NVX_GSP_PLATFORM_BUS_MASTER_FAILED,
    NVX_GSP_PLATFORM_STARTUP_FAILED,
    NVX_GSP_PLATFORM_BUSY,
    NVX_GSP_PLATFORM_QUIESCE_FAILED,
    NVX_GSP_PLATFORM_DMA_RECLAIM_FAILED,
    NVX_GSP_PLATFORM_SHUTDOWN_FAILED
} nvx_gsp_platform_status_t;

typedef struct nvx_gsp_platform_config {
    const struct pci_device *pci_device;
    /* BAR0 size must come from firmware/PCI resource ownership, not live probing. */
    const nvx_bar_resource_t *trusted_bar0;
    const nvx_firmware_provider_t *firmware_provider;
    const char *firmware_chip;       /* GA106 for the supported RTX 3060 profile. */
    const char *firmware_version;    /* Exact version shared by all five components. */
    /* Only explicitly collected, firmware-compatible GPU/platform facts belong here. */
    const nvx_gsp_system_info_t *trusted_gpu_facts;
    uint64_t command_queue_bytes;
    uint64_t status_queue_bytes;
    uint32_t gpu_instance;
    uint32_t timeout_ms;
    uint32_t poll_interval_ms;
    nvx_gsp_delay_ms_fn delay_ms;
    void *delay_context;
} nvx_gsp_platform_config_t;

/*
 * The quiesce callback must stop both SEC2 and GSP, drain/abort outstanding
 * transactions, and return true only when neither engine can issue further
 * DMA. It must not merely observe a timeout or clear a software flag.
 */
typedef int (*nvx_gsp_platform_quiesce_fn)(
    nvx_attached_device_t *device,
    const nvx_gsp_startup_result_t *startup_result,
    void *context);

/*
 * Persistent owner for every allocation and runtime object reachable by GSP.
 * Allocate this in static storage or long-lived kernel memory, never on a
 * transient stack. Keep it alive until nvx_gsp_platform_shutdown() succeeds.
 */
typedef struct nvx_gsp_platform_session {
    nvx_attached_device_t device;
    nvx_gsp_bundle_t firmware;
    nvx_gsp_boot_memory_t boot_memory;
    nvx_dma_buffer_t staged_vbios;
    nvx_dma_sync_t staged_vbios_sync;
    nvx_dma_buffer_t init_args;
    nvx_dma_sync_t init_args_sync;
    nvx_dma_buffer_t shared_queue;
    nvx_dma_sync_t shared_queue_sync;
    nvx_gsp_vbios_fwsec_info_t fwsec_info;
    nvx_gsp_ga10x_fb_facts_t framebuffer_facts;
    nvx_gsp_wpr_layout_t wpr_layout;
    nvx_gsp_modern_shared_queue_layout_t queue_layout;
    nvx_gsp_msgq_ga10x_t queue_runtime;
    nvx_gsp_gmcapi_transport_t gmcapi;
    nvx_gsp_msgq_rpc_transport_t rpc_transport;
    nvx_gsp_rpc_manager_t rpc_manager;
    nvx_gsp_startup_result_t startup_result;
    uint8_t encoded_system_info[NVX_GSP_PLATFORM_SYSTEM_INFO_CAPACITY];
    uint8_t static_info[NVX_GSP_PLATFORM_WORK_CAPACITY];
    uint8_t scratch[NVX_GSP_PLATFORM_WORK_CAPACITY];
    size_t encoded_system_info_size;
    size_t static_info_size;
    uint64_t wpr_metadata_phys;
    uint32_t fuse_version;
    uint8_t attached;
    uint8_t bus_master_enabled;
    uint8_t startup_attempted;
    uint8_t gsp_rm_initialized;
    uint8_t recovery_required;
    uint8_t quiesced;
} nvx_gsp_platform_session_t;

/* Validate all caller-supplied facts and sizing before touching PCI or hardware. */
nvx_gsp_platform_status_t nvx_gsp_platform_validate_config(
    const nvx_gsp_platform_config_t *config);

/*
 * Complete GA106 preparation and startup coordinator:
 * attach -> read VBIOS/FWSEC -> acquire and verify firmware -> stage SEC2
 * booters -> derive WPR from hardware facts -> allocate/build queue and init
 * args -> enable bus mastering -> run the composed SEC2/GSP/GSP-RM transaction.
 *
 * On failures before bus mastering/startup, resources are unwound. Once startup
 * is attempted, every allocation remains owned by session until an explicit
 * quiesce callback succeeds and shutdown returns DMA ownership to the CPU.
 */
nvx_gsp_platform_status_t nvx_gsp_platform_start(
    const nvx_gsp_platform_config_t *config,
    nvx_gsp_platform_session_t *session);

/* Stop hardware through the caller's verified quiesce operation, then reclaim. */
nvx_gsp_platform_status_t nvx_gsp_platform_shutdown(
    nvx_gsp_platform_session_t *session,
    nvx_gsp_platform_quiesce_fn quiesce,
    void *quiesce_context);

#endif
