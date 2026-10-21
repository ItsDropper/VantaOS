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
#include "nvx_gsp_fwsec_result.h"
#include "nvx_gsp_init_args.h"
#include "nvx_gsp_vbios.h"

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


typedef enum nvx_gsp_boot_phase {
    NVX_GSP_BOOT_PHASE_NONE = 0,
    NVX_GSP_BOOT_PHASE_PRECHECK,
    NVX_GSP_BOOT_PHASE_RESET_SEC2,
    NVX_GSP_BOOT_PHASE_TRANSFER_BOOTER_TO_DEVICE,
    NVX_GSP_BOOT_PHASE_UPLOAD_IMEM,
    NVX_GSP_BOOT_PHASE_UPLOAD_DMEM,
    NVX_GSP_BOOT_PHASE_RECLAIM_BOOTER,
    NVX_GSP_BOOT_PHASE_PROGRAM_BROM,
    NVX_GSP_BOOT_PHASE_TRANSFER_WPR_TO_DEVICE,
    NVX_GSP_BOOT_PHASE_START_SEC2,
    NVX_GSP_BOOT_PHASE_RECLAIM_WPR,
    NVX_GSP_BOOT_PHASE_VALIDATE_RESULT,
    NVX_GSP_BOOT_PHASE_PREPARE_RISCV,
    NVX_GSP_BOOT_PHASE_WAIT_RISCV_ACTIVE,
    NVX_GSP_BOOT_PHASE_COMPLETE
} nvx_gsp_boot_phase_t;

/*
 * Software evidence from one SEC2 Booter Load attempt. A phase is marked
 * completed only after its operation returned success. This is diagnostic
 * state, not proof that real firmware authenticated or booted on silicon.
 */
typedef struct nvx_gsp_boot_trace {
    nvx_gsp_boot_phase_t current_phase;
    nvx_gsp_boot_phase_t last_completed_phase;
    nvx_gsp_boot_phase_t failed_phase;
    nvx_gsp_falcon_status_t result;
    nvx_gsp_falcon_status_t recovery_trigger_status;
    nvx_gsp_falcon_status_t recovery_result;
    uint8_t recovery_attempted;
    uint8_t recovery_succeeded;
    uint8_t sec2_quiescence_confirmed;
    uint8_t riscv_active;
} nvx_gsp_boot_trace_t;

/*
 * Execute FWSEC-FRTS on the GA10x GSP Falcon using a DMA-staged VBIOS image.
 * The VBIOS descriptor/signature slot must already be structurally validated.
 * This routine patches the signed FWSEC DMEM image, uploads secure IMEM/DMEM,
 * programs BROM verification parameters, waits for halt, then validates actual
 * FWSEC result registers before returning success.
 */
nvx_gsp_falcon_status_t nvx_gsp_ga10x_execute_fwsec_frts(
    nvx_dma_buffer_t *staged_vbios, nvx_dma_sync_t *staged_vbios_sync,
    const nvx_gsp_vbios_fwsec_info_t *info, uint32_t fuse_version,
    const nvx_mmio_region_t *bar0, uint64_t frts_framebuffer_offset,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    nvx_gsp_fwsec_result_snapshot_t *out_result);

/*
 * Prepare the GA102 GSP core after successful FRTS: transfer the radix3 ELF
 * and materialized GSP_ARGUMENTS_CACHED buffer to device ownership, reset into
 * RISC-V mode with BR_FETCH, and program the init-arguments SYSRAM address in
 * the GSP mailboxes. The caller must have built the shared-queue PTE table in
 * shared_queue_mem. This does not claim GSP-RM is active; SEC2 Booter Load must
 * run before nvx_gsp_ga10x_wait_riscv_active() can establish readiness.
 */
nvx_gsp_falcon_status_t nvx_gsp_ga10x_prepare_riscv_after_frts(
    nvx_gsp_boot_memory_t *memory,
    nvx_dma_buffer_t *libos_init_args,
    nvx_dma_sync_t *libos_init_args_sync,
    nvx_dma_buffer_t *shared_queue_mem,
    nvx_dma_sync_t *shared_queue_sync,
    const nvx_gsp_modern_shared_queue_layout_t *queue_layout,
    const nvx_mmio_region_t *bar0, uint64_t frts_framebuffer_offset,
    const nvx_gsp_fwsec_result_snapshot_t *frts_result,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context);

/*
 * Run the normal GA10x bootstrap sequence in hardware order:
 * FWSEC-FRTS -> reset GSP into RISC-V and publish init-args address ->
 * SEC2 Booter Load -> bounded RISC-V active check.
 * A successful return proves the RISC-V active bit was observed, not that
 * GSP-RM RPC initialization or hardware acceleration has completed.
 */
nvx_gsp_falcon_status_t nvx_gsp_ga10x_bootstrap_traced(
    nvx_dma_buffer_t *staged_vbios, nvx_dma_sync_t *staged_vbios_sync,
    const nvx_gsp_vbios_fwsec_info_t *fwsec_info, uint32_t fuse_version,
    nvx_gsp_boot_memory_t *memory,
    nvx_dma_buffer_t *libos_init_args,
    nvx_dma_sync_t *libos_init_args_sync,
    nvx_dma_buffer_t *shared_queue_mem,
    nvx_dma_sync_t *shared_queue_sync,
    const nvx_mmio_region_t *bar0, uint64_t frts_framebuffer_offset,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    nvx_gsp_fwsec_result_snapshot_t *out_frts_result,
    uint32_t *out_booter_mailbox1, nvx_gsp_boot_trace_t *out_trace);

/*
 * Execute the signed GA10x Booter Load image on SEC2:
 * reset -> secure IMEM DMA -> DMEM DMA -> BROM PKC parameters ->
 * SEC2 start with WPR metadata SYSRAM address in mailboxes -> halt/result.
 *
 * This runs real MMIO. Caller must have mapped the correct GA10x BAR0,
 * acquired and staged the booter via nvx_gsp_boot_memory_stage_booters_from_bar0,
 * prepared WPR metadata, and supplied a scheduler delay callback.
 */
nvx_gsp_falcon_status_t nvx_gsp_ga10x_execute_booter_load_traced(
    nvx_gsp_boot_memory_t *memory, const nvx_mmio_region_t *bar0,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    uint32_t *out_mailbox1, nvx_gsp_boot_trace_t *out_trace);

/* Compatibility entry point; identical execution, without phase diagnostics. */
nvx_gsp_falcon_status_t nvx_gsp_ga10x_execute_booter_load(
    nvx_gsp_boot_memory_t *memory, const nvx_mmio_region_t *bar0,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    uint32_t *out_mailbox1);

#endif
