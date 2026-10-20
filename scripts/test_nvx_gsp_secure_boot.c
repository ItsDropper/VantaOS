/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_secure_boot.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct fake_hardware {
    nvx_mmio_region_t *bar0;
    uint8_t hold_dma_busy;
    uint8_t stick_scrub_on_dma_timeout;
    uint8_t hold_dmem_dma;
} fake_hardware_t;

static void fake_delay(uint32_t milliseconds, void *context)
{
    fake_hardware_t *hardware = (fake_hardware_t *)context;
    nvx_mmio_region_t *bar0 = hardware->bar0;
    (void)milliseconds;
    uint32_t dma_command = NVX_GA10X_FALCON_DMA_IDLE;
    uint32_t dma_source_offset = 0U;
    assert(nvx_mmio_read32(bar0,
        NVX_GA10X_FALCON_DMATRFCMD(NVX_SEC2_FALCON_BASE),
        &dma_command) == NVX_MMIO_OK);
    assert(nvx_mmio_read32(bar0,
        NVX_GA10X_FALCON_DMATRFFBOFFS(NVX_SEC2_FALCON_BASE),
        &dma_source_offset) == NVX_MMIO_OK);
    if (dma_command != NVX_GA10X_FALCON_DMA_IDLE &&
        hardware->hold_dma_busy && hardware->stick_scrub_on_dma_timeout) {
        assert(nvx_mmio_write32(bar0, NVX_SEC2_FALCON_BASE + 0x0F4U,
            NVX_GSP_HWCFG2_MEMORY_SCRUBBING) == NVX_MMIO_OK);
    }
    if (!hardware->hold_dma_busy &&
        !(hardware->hold_dmem_dma && dma_source_offset >= 0x2000U)) {
        assert(nvx_mmio_write32(bar0,
            NVX_GA10X_FALCON_DMATRFCMD(NVX_SEC2_FALCON_BASE),
            NVX_GA10X_FALCON_DMA_IDLE) == NVX_MMIO_OK);
    }
    assert(nvx_mmio_write32(bar0, NVX_SEC2_FALCON_BASE + 0x100U,
        NVX_GSP_CPUCTL_HALTED) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(bar0, NVX_SEC2_FALCON_BASE + 0x040U,
        0U) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(bar0, NVX_SEC2_FALCON_BASE + 0x044U,
        0xB007U) == NVX_MMIO_OK);
}

static uint32_t read_reg(const nvx_mmio_region_t *bar0, uint32_t offset)
{
    uint32_t value = UINT32_MAX;
    assert(nvx_mmio_read32(bar0, offset, &value) == NVX_MMIO_OK);
    return value;
}

int main(void)
{
    uint8_t *bar_bytes = calloc(1U, NVX_GA10X_BAR0_REQUIRED_BYTES);
    uint8_t *load_bytes = calloc(1U, 0x4000U);
    uint8_t *metadata_bytes = calloc(1U, NVX_DMA_PAGE_SIZE);
    nvx_mmio_region_t bar0;
    fake_hardware_t hardware = {0};
    nvx_dma_buffer_t load_buffer = {
        .cpu_address = load_bytes, .gpu_address = 0x10000000ULL,
        .size = 0x4000U, .allocated_size = 0x4000U, .page_count = 4U,
        .allocation_id = 11U, .acquired = 1U
    };
    nvx_dma_buffer_t metadata_buffer = {
        .cpu_address = metadata_bytes, .gpu_address = 0x20000000ULL,
        .size = NVX_DMA_PAGE_SIZE, .allocated_size = NVX_DMA_PAGE_SIZE,
        .page_count = 1U, .allocation_id = 12U, .acquired = 1U
    };
    nvx_gsp_boot_memory_t memory = {0};
    nvx_gsp_image_info_t info = {
        .data_size = 0x4000U,
        .engine_id = 0x0001U,
        .ucode_id = 2U,
        .signature_count = 1U,
        .signature_size = 0x100U,
        .patch_location = 0x2100U,
        .os_code_size = 0x1000U,
        .os_data_offset = 0x2000U,
        .os_data_size = 0x1000U,
        .app_count = 1U,
        .app0_offset = 0x1000U,
        .app0_size = 0x1000U
    };
    nvx_gsp_secure_transfer_plan_t plan;
    uint32_t mailbox1 = 0U;
    nvx_gsp_boot_trace_t trace;

    assert(bar_bytes && load_bytes && metadata_bytes);
    bar0.base = (volatile uint8_t *)bar_bytes;
    bar0.length = NVX_GA10X_BAR0_REQUIRED_BYTES;
    hardware.bar0 = &bar0;
    assert(nvx_mmio_write32(&bar0, NVX_SEC2_FALCON_BASE + 0x0F4U, 0U) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(&bar0, NVX_SEC2_FALCON_BASE + 0x100U,
                            NVX_GSP_CPUCTL_HALTED) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(&bar0,
        NVX_GA10X_FALCON_DMATRFCMD(NVX_SEC2_FALCON_BASE),
        NVX_GA10X_FALCON_DMA_IDLE) == NVX_MMIO_OK);

    assert(nvx_gsp_ga10x_secure_image_plan(&load_buffer, &info, &plan) ==
           NVX_GSP_FALCON_OK);
    assert(plan.source_phys == load_buffer.gpu_address);
    assert(plan.imem_source_offset == 0x1000U);
    assert(plan.imem_destination_offset == 0U);
    assert(plan.imem_size == 0x1000U);
    assert(plan.dmem_source_offset == 0x2000U);
    assert(plan.dmem_destination_offset == 0U);
    assert(plan.dmem_size == 0x1000U);
    assert(plan.boot_vector == 0x1000U);
    assert(plan.signature_dmem_offset == 0x100U);
    assert(plan.engine_id_mask == 1U && plan.ucode_id == 2U);

    info.app0_offset++;
    assert(nvx_gsp_ga10x_secure_image_plan(&load_buffer, &info, &plan) ==
           NVX_GSP_FALCON_IMAGE_INVALID);
    info.app0_offset = 0x1000U;
    info.engine_id = 0x0002U;
    assert(nvx_gsp_ga10x_secure_image_plan(&load_buffer, &info, &plan) ==
           NVX_GSP_FALCON_BROM_INVALID_PARAMETERS);
    info.engine_id = 0x0001U;
    info.patch_location = 0x3F80U;
    assert(nvx_gsp_ga10x_secure_image_plan(&load_buffer, &info, &plan) ==
           NVX_GSP_FALCON_IMAGE_INVALID);
    info.patch_location = 0x2100U;

    memory.acquired = 1U;
    memory.booter_load = load_buffer;
    memory.wpr_metadata = metadata_buffer;
    memory.booter_load_info = info;
    memory.wpr_metadata_prepared = 0U;
    assert(nvx_dma_sync_init(&memory.booter_load_sync,
                             &memory.booter_load) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_init(&memory.wpr_metadata_sync,
                             &memory.wpr_metadata) == NVX_DMA_SYNC_OK);
    assert(nvx_gsp_ga10x_execute_booter_load(&memory, &bar0, 20U, 1U,
        fake_delay, &hardware, &mailbox1) == NVX_GSP_FALCON_BOOTER_NOT_STAGED);
    memory.booters_staged = 1U;
    assert(nvx_gsp_ga10x_execute_booter_load_traced(&memory, &bar0, 20U, 1U,
        fake_delay, &hardware, &mailbox1, &trace) == NVX_GSP_FALCON_WPR_METADATA_NOT_READY);
    assert(trace.result == NVX_GSP_FALCON_WPR_METADATA_NOT_READY);
    assert(trace.failed_phase == NVX_GSP_BOOT_PHASE_PRECHECK);
    assert(trace.last_completed_phase == NVX_GSP_BOOT_PHASE_NONE);
    assert(trace.recovery_attempted == 0U);
    memory.wpr_metadata_prepared = 1U;

    /* Fault injection: hold SEC2 DMA busy through timeout and verify recovery. */
    hardware.hold_dma_busy = 1U;
    assert(nvx_gsp_ga10x_execute_booter_load_traced(&memory, &bar0, 2U, 1U,
        fake_delay, &hardware, &mailbox1, &trace) == NVX_GSP_FALCON_DMA_TIMEOUT);
    assert(trace.result == NVX_GSP_FALCON_DMA_TIMEOUT);
    assert(trace.failed_phase == NVX_GSP_BOOT_PHASE_UPLOAD_IMEM);
    assert(trace.last_completed_phase == NVX_GSP_BOOT_PHASE_TRANSFER_BOOTER_TO_DEVICE);
    assert(trace.recovery_attempted == 1U);
    assert(trace.recovery_trigger_status == NVX_GSP_FALCON_DMA_TIMEOUT);
    assert(trace.recovery_result == NVX_GSP_FALCON_OK);
    assert(trace.recovery_succeeded == 1U);
    assert(trace.sec2_quiescence_confirmed == 1U);
    assert(memory.booter_load_sync.owner == NVX_DMA_OWNER_CPU);
    hardware.hold_dma_busy = 0U;
    assert(nvx_mmio_write32(&bar0,
        NVX_GA10X_FALCON_DMATRFCMD(NVX_SEC2_FALCON_BASE),
        NVX_GA10X_FALCON_DMA_IDLE) == NVX_MMIO_OK);

    /* Fail DMEM DMA specifically by its source offset; IMEM remains successful.
       This avoids depending on a guessed count of polling delays. */
    hardware.hold_dmem_dma = 1U;
    assert(nvx_gsp_ga10x_execute_booter_load_traced(&memory, &bar0, 2U, 1U,
        fake_delay, &hardware, &mailbox1, &trace) == NVX_GSP_FALCON_DMA_TIMEOUT);
    assert(trace.failed_phase == NVX_GSP_BOOT_PHASE_UPLOAD_DMEM);
    assert(trace.last_completed_phase == NVX_GSP_BOOT_PHASE_UPLOAD_IMEM);
    assert(trace.recovery_attempted == 1U);
    assert(trace.recovery_trigger_status == NVX_GSP_FALCON_DMA_TIMEOUT);
    assert(trace.recovery_result == NVX_GSP_FALCON_OK);
    assert(trace.recovery_succeeded == 1U);
    assert(trace.sec2_quiescence_confirmed == 1U);
    assert(memory.booter_load_sync.owner == NVX_DMA_OWNER_CPU);
    hardware.hold_dmem_dma = 0U;
    assert(nvx_mmio_write32(&bar0,
        NVX_GA10X_FALCON_DMATRFCMD(NVX_SEC2_FALCON_BASE),
        NVX_GA10X_FALCON_DMA_IDLE) == NVX_MMIO_OK);

    /* Second fault: DMA timeout followed by a reset that never leaves scrub.
       Recovery must not claim quiescence or return device-owned memory. */
    hardware.hold_dma_busy = 1U;
    hardware.stick_scrub_on_dma_timeout = 1U;
    assert(nvx_gsp_ga10x_execute_booter_load_traced(&memory, &bar0, 2U, 1U,
        fake_delay, &hardware, &mailbox1, &trace) == NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR);
    assert(trace.result == NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR);
    assert(trace.failed_phase == NVX_GSP_BOOT_PHASE_UPLOAD_IMEM);
    assert(trace.recovery_attempted == 1U);
    assert(trace.recovery_trigger_status == NVX_GSP_FALCON_DMA_TIMEOUT);
    assert(trace.recovery_result == NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR);
    assert(trace.recovery_succeeded == 0U);
    assert(trace.sec2_quiescence_confirmed == 0U);
    assert(memory.booter_load_sync.owner == NVX_DMA_OWNER_DEVICE);

    /* Restore the synthetic BAR only for test cleanup; this is not a driver
       recovery result and must not change the assertions above. */
    hardware.hold_dma_busy = 0U;
    hardware.stick_scrub_on_dma_timeout = 0U;
    assert(nvx_mmio_write32(&bar0, NVX_SEC2_FALCON_BASE + 0x0F4U,
        0U) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(&bar0,
        NVX_GA10X_FALCON_DMATRFCMD(NVX_SEC2_FALCON_BASE),
        NVX_GA10X_FALCON_DMA_IDLE) == NVX_MMIO_OK);
    assert(nvx_dma_sync_for_cpu(&memory.booter_load_sync, 0U,
        memory.booter_load.size) == NVX_DMA_SYNC_OK);

    assert(nvx_gsp_ga10x_execute_booter_load_traced(&memory, &bar0, 20U, 1U,
        fake_delay, &hardware, &mailbox1, &trace) == NVX_GSP_FALCON_OK);
    assert(trace.result == NVX_GSP_FALCON_OK);
    assert(trace.current_phase == NVX_GSP_BOOT_PHASE_COMPLETE);
    assert(trace.last_completed_phase == NVX_GSP_BOOT_PHASE_COMPLETE);
    assert(trace.failed_phase == NVX_GSP_BOOT_PHASE_NONE);
    assert(trace.sec2_quiescence_confirmed == 1U);
    assert(trace.recovery_attempted == 0U);
    assert(mailbox1 == 0xB007U);
    assert(memory.booter_load_sync.owner == NVX_DMA_OWNER_CPU);
    assert(memory.wpr_metadata_sync.owner == NVX_DMA_OWNER_CPU);
    assert(read_reg(&bar0, NVX_GA10X_FALCON_BROM_PARAM_ADDR(NVX_SEC2_FALCON2_BASE)) == 0x100U);
    assert(read_reg(&bar0, NVX_GA10X_FALCON_BROM_ENGINE_MASK(NVX_SEC2_FALCON2_BASE)) == 1U);
    assert(read_reg(&bar0, NVX_GA10X_FALCON_BROM_UCODE_ID(NVX_SEC2_FALCON2_BASE)) == 2U);
    assert(read_reg(&bar0, NVX_GA10X_FALCON_BROM_MOD_SEL(NVX_SEC2_FALCON2_BASE)) ==
           NVX_GSP_FALCON_BROM_RSA3K);
    assert(read_reg(&bar0, NVX_SEC2_FALCON_BASE + 0x104U) == 0x1000U);

    assert(nvx_dma_sync_destroy(&memory.booter_load_sync) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_destroy(&memory.wpr_metadata_sync) == NVX_DMA_SYNC_OK);
    free(metadata_bytes);
    free(load_bytes);
    free(bar_bytes);
    return 0;
}
