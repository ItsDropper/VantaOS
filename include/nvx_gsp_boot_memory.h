/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_BOOT_MEMORY_H
#define VANTA_NVX_GSP_BOOT_MEMORY_H
#include "nvx_dma_sync.h"
#include "nvx_gsp_bundle.h"
#include "nvx_gsp_preflight.h"
#include "nvx_gsp_wpr_meta.h"

typedef enum nvx_gsp_boot_memory_status {
    NVX_GSP_BOOT_MEMORY_OK = 0,
    NVX_GSP_BOOT_MEMORY_INVALID_ARGUMENT,
    NVX_GSP_BOOT_MEMORY_PREFLIGHT_FAILED,
    NVX_GSP_BOOT_MEMORY_ELF_SECTION_FAILED,
    NVX_GSP_BOOT_MEMORY_ALLOCATION_FAILED,
    NVX_GSP_BOOT_MEMORY_COPY_FAILED,
    NVX_GSP_BOOT_MEMORY_WPR_METADATA_FAILED,
    NVX_GSP_BOOT_MEMORY_BOOTER_STAGE_FAILED
} nvx_gsp_boot_memory_status_t;

/*
 * Contiguous system-memory artifacts used by the GA10x secure boot path.
 * These are not framebuffer/WPR allocations and this routine does not start
 * firmware. The platform must still construct the verified WPR layout and
 * arrange the documented FWSEC/bootloader transaction.
 */
typedef struct nvx_gsp_boot_memory {
    nvx_dma_buffer_t radix3_elf;
    nvx_dma_buffer_t bootloader_payload;
    nvx_dma_buffer_t signature;
    nvx_dma_buffer_t booter_load;
    nvx_dma_buffer_t booter_unload;
    nvx_dma_buffer_t wpr_metadata;
    nvx_dma_sync_t booter_load_sync;
    nvx_dma_sync_t booter_unload_sync;
    nvx_dma_sync_t wpr_metadata_sync;
    nvx_gsp_image_info_t booter_load_info;
    nvx_gsp_image_info_t booter_unload_info;
    uint32_t monitor_code_offset, monitor_data_offset, manifest_offset;
    uint32_t monitor_code_size, monitor_data_size, manifest_size;
    uint8_t booters_staged;
    uint8_t wpr_metadata_prepared;
    uint8_t acquired;
} nvx_gsp_boot_memory_t;

/* Zero-initialize before first use; on any error all allocations are released. */
nvx_gsp_boot_memory_status_t nvx_gsp_boot_memory_acquire(
    const nvx_gsp_bundle_t *bundle, nvx_gsp_boot_memory_t *out_memory);
/* Release only when all DMA ownership has returned to the CPU. If an engine may
 * still own a buffer, this function leaves the allocation intact for safe recovery. */
void nvx_gsp_boot_memory_release(nvx_gsp_boot_memory_t *memory);
/* Bind real allocated SYSRAM addresses into a caller-planned framebuffer/WPR layout. */
nvx_gsp_boot_memory_status_t nvx_gsp_boot_memory_bind_wpr(
    const nvx_gsp_boot_memory_t *memory, nvx_gsp_wpr_layout_t *layout);
/* Stage fuse-selected booter payloads using actual GA10x BAR0 fuse values. */
nvx_gsp_boot_memory_status_t nvx_gsp_boot_memory_stage_booters_from_bar0(
    nvx_gsp_boot_memory_t *memory, const nvx_gsp_bundle_t *bundle,
    const nvx_mmio_region_t *bar0);
/* Serialize planned WPR metadata into a dedicated page-aligned SYSRAM DMA buffer. */
nvx_gsp_boot_memory_status_t nvx_gsp_boot_memory_prepare_wpr_metadata(
    nvx_gsp_boot_memory_t *memory, const nvx_gsp_wpr_layout_t *planned_layout,
    uint64_t *out_metadata_phys);
#endif
