/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_BOOT_MEMORY_H
#define VANTA_NVX_GSP_BOOT_MEMORY_H
#include "nvx_dma.h"
#include "nvx_gsp_bundle.h"
#include "nvx_gsp_preflight.h"

typedef enum nvx_gsp_boot_memory_status {
    NVX_GSP_BOOT_MEMORY_OK = 0,
    NVX_GSP_BOOT_MEMORY_INVALID_ARGUMENT,
    NVX_GSP_BOOT_MEMORY_PREFLIGHT_FAILED,
    NVX_GSP_BOOT_MEMORY_ELF_SECTION_FAILED,
    NVX_GSP_BOOT_MEMORY_ALLOCATION_FAILED,
    NVX_GSP_BOOT_MEMORY_COPY_FAILED
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
    uint8_t acquired;
} nvx_gsp_boot_memory_t;

/* Zero-initialize before first use; on any error all allocations are released. */
nvx_gsp_boot_memory_status_t nvx_gsp_boot_memory_acquire(
    const nvx_gsp_bundle_t *bundle, nvx_gsp_boot_memory_t *out_memory);
void nvx_gsp_boot_memory_release(nvx_gsp_boot_memory_t *memory);
#endif
