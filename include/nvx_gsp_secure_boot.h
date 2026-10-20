/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_SECURE_BOOT_H
#define VANTA_NVX_GSP_SECURE_BOOT_H

#include "nvx_gsp_boot_memory.h"
#include "nvx_gsp_falcon.h"

typedef struct nvx_gsp_secure_transfer_plan {
    uint64_t source_phys;
    uint32_t imem_source_offset;
    uint32_t imem_destination_offset;
    uint32_t imem_size;
    uint32_t dmem_source_offset;
    uint32_t dmem_destination_offset;
    uint32_t dmem_size;
    uint32_t boot_vector;
    uint32_t signature_dmem_offset;
    uint32_t engine_id_mask;
    uint32_t ucode_id;
} nvx_gsp_secure_transfer_plan_t;

/*
 * Translate a structurally parsed GA10x high-secure image descriptor into
 * the exact SEC2 IMEM/DMEM DMA operations. No MMIO is performed.
 */
nvx_gsp_falcon_status_t nvx_gsp_ga10x_secure_image_plan(
    const nvx_dma_buffer_t *staged_image,
    const nvx_gsp_image_info_t *image,
    nvx_gsp_secure_transfer_plan_t *out_plan);

/*
 * Execute the signed GA10x Booter Load image on SEC2:
 * reset -> secure IMEM DMA -> DMEM DMA -> BROM PKC parameters ->
 * SEC2 start with WPR metadata SYSRAM address in mailboxes -> halt/result.
 *
 * This runs real MMIO. Caller must have mapped the correct GA10x BAR0,
 * acquired and staged the booter via nvx_gsp_boot_memory_stage_booters_from_bar0,
 * prepared WPR metadata, and supplied a scheduler delay callback.
 */
nvx_gsp_falcon_status_t nvx_gsp_ga10x_execute_booter_load(
    nvx_gsp_boot_memory_t *memory, const nvx_mmio_region_t *bar0,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    uint32_t *out_mailbox1);

#endif
