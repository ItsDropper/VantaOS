/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_secure_boot.h"
#include "nvx_gsp_fwsec_mmio.h"
#include "nvx_gsp_vbios.h"
#include "nvx_gsp_init_args.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TEST_QUEUE_SIZE UINT64_C(0x40000)
#define TEST_SHARED_SIZE (NVX_GSP_PAGE_SIZE + 2U * TEST_QUEUE_SIZE)

typedef struct fake_hardware {
    nvx_mmio_region_t *bar0;
    uint8_t hold_dma_busy;
    uint8_t stick_scrub_on_dma_timeout;
    uint8_t hold_dmem_dma;
    uint8_t activate_gsp_on_start;
    uint8_t activate_gsp_after_sec2;
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
    uint32_t sec2_cpuctl = 0U;
    assert(nvx_mmio_read32(bar0, NVX_SEC2_FALCON_BASE + 0x100U,
                           &sec2_cpuctl) == NVX_MMIO_OK);
    if (hardware->activate_gsp_after_sec2 &&
        (sec2_cpuctl & NVX_GSP_CPUCTL_START) != 0U)
        assert(nvx_mmio_write32(bar0, NVX_GSP_RISCV_CPUCTL,
                                NVX_GSP_RISCV_ACTIVE) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(bar0, NVX_SEC2_FALCON_BASE + 0x100U,
        NVX_GSP_CPUCTL_HALTED) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(bar0, NVX_SEC2_FALCON_BASE + 0x040U,
        0U) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(bar0, NVX_SEC2_FALCON_BASE + 0x044U,
        0xB007U) == NVX_MMIO_OK);

    uint32_t gsp_engine = 0U;
    assert(nvx_mmio_read32(bar0, NVX_GA10X_FALCON_ENGINE(NVX_GSP_FALCON_BASE),
                           &gsp_engine) == NVX_MMIO_OK);
    if (gsp_engine != 0U)
        assert(nvx_mmio_write32(bar0, NVX_GSP_RISCV_CPUCTL, 0U) == NVX_MMIO_OK);

    uint32_t gsp_dma_command = NVX_GA10X_FALCON_DMA_IDLE;
    assert(nvx_mmio_read32(bar0, NVX_GA10X_FALCON_DMATRFCMD(NVX_GSP_FALCON_BASE),
                           &gsp_dma_command) == NVX_MMIO_OK);
    if (gsp_dma_command != NVX_GA10X_FALCON_DMA_IDLE)
        assert(nvx_mmio_write32(bar0, NVX_GA10X_FALCON_DMATRFCMD(NVX_GSP_FALCON_BASE),
                                NVX_GA10X_FALCON_DMA_IDLE) == NVX_MMIO_OK);

    uint32_t gsp_cpuctl = 0U;
    assert(nvx_mmio_read32(bar0, NVX_GSP_FALCON_CPUCTL,
                           &gsp_cpuctl) == NVX_MMIO_OK);
    if (hardware->activate_gsp_on_start &&
        (gsp_cpuctl & NVX_GSP_CPUCTL_START) != 0U) {
        assert(nvx_mmio_write32(bar0, NVX_GSP_FALCON_CPUCTL,
                                NVX_GSP_CPUCTL_HALTED) == NVX_MMIO_OK);
        assert(nvx_mmio_write32(bar0, NVX_GSP_RISCV_CPUCTL,
                                NVX_GSP_RISCV_ACTIVE) == NVX_MMIO_OK);
        assert(nvx_mmio_write32(bar0, NVX_GSP_FALCON_MAILBOX0,
                                0U) == NVX_MMIO_OK);
        assert(nvx_mmio_write32(bar0, NVX_GSP_FALCON_MAILBOX1,
                                0xB007U) == NVX_MMIO_OK);
    }
}

static void put_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

static void put_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
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
    uint8_t *radix_bytes = calloc(1U, 0x4000U);
    uint8_t *vbios_bytes = calloc(1U, 0x4000U);
    uint8_t *init_args_bytes = aligned_alloc(NVX_GSP_INIT_ARGS_PAGE_SIZE, NVX_GSP_INIT_ARGS_PAGE_SIZE);
    uint8_t *shared_queue_bytes = aligned_alloc(NVX_GSP_PAGE_SIZE, (size_t)TEST_SHARED_SIZE);
    nvx_mmio_region_t bar0;
    fake_hardware_t hardware = {0};
    nvx_dma_buffer_t load_buffer = {
        .cpu_address = load_bytes, .gpu_address = 0x10000000ULL,
        .size = 0x4000U, .allocated_size = 0x4000U, .page_count = 4U,
        .allocation_id = 11U, .acquired = 1U
    };
    nvx_dma_buffer_t radix_buffer = {
        .cpu_address = radix_bytes, .gpu_address = 0x30000000ULL,
        .size = 0x4000U, .allocated_size = 0x4000U, .page_count = 4U,
        .allocation_id = 13U, .acquired = 1U
    };
    nvx_dma_buffer_t metadata_buffer = {
        .cpu_address = metadata_bytes, .gpu_address = 0x20000000ULL,
        .size = NVX_DMA_PAGE_SIZE, .allocated_size = NVX_DMA_PAGE_SIZE,
        .page_count = 1U, .allocation_id = 12U, .acquired = 1U
    };
    nvx_dma_buffer_t vbios_buffer = {
        .cpu_address = vbios_bytes, .gpu_address = 0x40000000ULL,
        .size = 0x4000U, .allocated_size = 0x4000U, .page_count = 4U,
        .allocation_id = 14U, .acquired = 1U
    };
    nvx_dma_buffer_t init_args_buffer = {
        .cpu_address = init_args_bytes, .gpu_address = 0x50000000ULL,
        .size = NVX_GSP_INIT_ARGS_PAGE_SIZE,
        .allocated_size = NVX_GSP_INIT_ARGS_PAGE_SIZE, .page_count = 1U,
        .allocation_id = 15U, .acquired = 1U
    };
    nvx_dma_buffer_t shared_queue_buffer = {
        .cpu_address = shared_queue_bytes, .gpu_address = 0x60000000ULL,
        .size = (size_t)TEST_SHARED_SIZE, .allocated_size = (size_t)TEST_SHARED_SIZE,
        .page_count = (size_t)(TEST_SHARED_SIZE / NVX_GSP_PAGE_SIZE),
        .allocation_id = 16U, .acquired = 1U
    };
    nvx_dma_sync_t vbios_sync = {0};
    nvx_dma_sync_t init_args_sync = {0};
    nvx_dma_sync_t shared_queue_sync = {0};
    nvx_gsp_modern_shared_queue_config_t queue_config = {0};
    nvx_gsp_modern_shared_queue_layout_t queue_layout = {0};
    nvx_gsp_arguments_cached_modern_t init_args = {0};
    nvx_gsp_vbios_fwsec_info_t fwsec_info = {
        .image_offset = 0x1000U, .image_size = 0x2000U,
        .imem_physical_base = 0U, .imem_load_size = 0x1000U,
        .imem_virtual_base = 0x1000U, .dmem_physical_base = 0U,
        .dmem_load_size = 0x1000U, .interface_offset = 0x100U,
        .pkc_data_offset = 0x700U, .signatures_offset = 0x200U,
        .signatures_size = NVX_GSP_VBIOS_RSA3K_SIGNATURE_SIZE,
        .engine_id_mask = NVX_GSP_VBIOS_FWSEC_ENGINE_MASK,
        .signature_versions = 1U, .ucode_id = 2U, .signature_count = 1U,
        .descriptor_version = NVX_GSP_VBIOS_FWSEC_DESC_V3, .encrypted = 0U
    };
    nvx_gsp_fwsec_result_snapshot_t executed_frts = {0};
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
    nvx_gsp_fwsec_result_snapshot_t frts_result = {
        .frts_error_code = 0U, .wpr2_addr_hi = 1U,
        .wpr2_addr_lo_units = 0x1000U,
        .expected_wpr2_addr_lo_units = 0x1000U
    };

    assert(bar_bytes && load_bytes && metadata_bytes && radix_bytes && vbios_bytes &&
           init_args_bytes && shared_queue_bytes);
    bar0.base = (volatile uint8_t *)bar_bytes;
    bar0.length = NVX_GA10X_BAR0_REQUIRED_BYTES;
    hardware.bar0 = &bar0;
    assert(nvx_mmio_write32(&bar0, NVX_GSP_FALCON_HWCFG2,
        NVX_GSP_HWCFG2_RESET_READY | NVX_GSP_HWCFG2_RISCV_PRESENT) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(&bar0, NVX_GSP_FALCON_CPUCTL,
        NVX_GSP_CPUCTL_HALTED) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(&bar0, NVX_GA10X_FALCON_DMATRFCMD(NVX_GSP_FALCON_BASE),
        NVX_GA10X_FALCON_DMA_IDLE) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(&bar0, NVX_GSP_FWSEC_VBIOS_SCRATCH(NVX_GSP_FWSEC_SCRATCH_FRTS_INDEX), 0U) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(&bar0, NVX_GSP_FWSEC_VBIOS_SCRATCH(NVX_GSP_FWSEC_SCRATCH_SB_INDEX), 0U) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(&bar0, NVX_GSP_FWSEC_WPR2_ADDR_LO, 0x10000U) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(&bar0, NVX_GSP_FWSEC_WPR2_ADDR_HI, 0x10U) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(&bar0, NVX_GSP_FWSEC_GFW_PLM, NVX_GSP_FWSEC_PLM_READ_LEVEL0_ENABLE) == NVX_MMIO_OK);
    assert(nvx_mmio_write32(&bar0, NVX_GSP_FWSEC_GFW_PROGRESS, NVX_GSP_FWSEC_GFW_PROGRESS_COMPLETED) == NVX_MMIO_OK);
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
    memory.radix3_elf = radix_buffer;
    memory.booter_load = load_buffer;
    memory.wpr_metadata = metadata_buffer;
    memory.booter_load_info = info;
    memory.wpr_metadata_prepared = 0U;
    assert(nvx_dma_sync_init(&memory.radix3_elf_sync,
                             &memory.radix3_elf) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_init(&memory.booter_load_sync,
                             &memory.booter_load) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_init(&memory.wpr_metadata_sync,
                             &memory.wpr_metadata) == NVX_DMA_SYNC_OK);
    /* Synthetic FWSEC V3 DMEM interface and signed signature slot. */
    {
        uint8_t *dmem = vbios_bytes + 0x2000U;
        memset(vbios_bytes + 0x200U, 0x5AU, NVX_GSP_VBIOS_RSA3K_SIGNATURE_SIZE);
        dmem[0x100U] = 1U;
        dmem[0x101U] = 4U;
        dmem[0x102U] = 8U;
        dmem[0x103U] = 2U;
        put_le32(dmem + 0x104U, 4U);
        put_le32(dmem + 0x108U, 0x200U);
        put_le32(dmem + 0x10CU, 1U);
        put_le32(dmem + 0x110U, 0U);
        put_le16(dmem + 0x204U, 3U);
        put_le16(dmem + 0x206U, 64U);
        put_le32(dmem + 0x208U, 0x500U);
        put_le32(dmem + 0x20CU, 0x100U);
    }
    assert(nvx_dma_sync_init(&vbios_sync, &vbios_buffer) == NVX_DMA_SYNC_OK);

    queue_config.shared_mem_phys_addr = shared_queue_buffer.gpu_address;
    queue_config.allocation_size = shared_queue_buffer.size;
    queue_config.command_queue_size = TEST_QUEUE_SIZE;
    queue_config.status_queue_size = TEST_QUEUE_SIZE;
    queue_config.queue_element_header_size = NVX_GSP_QUEUE_HEADER_SIZE_PLAIN;
    queue_config.queue_element_size_min = NVX_GSP_QUEUE_ELEMENT_MIN_SIZE;
    queue_config.queue_element_size_max = NVX_GSP_QUEUE_ELEMENT_MAX_SIZE;
    queue_config.queue_header_alignment = NVX_GSP_QUEUE_HEADER_ALIGNMENT;
    queue_config.queue_element_alignment = NVX_GSP_QUEUE_ELEMENT_ALIGNMENT;
    assert(nvx_gsp_modern_shared_queue_plan(&queue_config, &queue_layout) ==
           NVX_GSP_INIT_ARGS_OK);
    assert(nvx_gsp_modern_shared_queue_build_pte_table(
        &queue_layout, shared_queue_bytes, shared_queue_buffer.size) ==
        NVX_GSP_INIT_ARGS_OK);
    assert(nvx_gsp_arguments_cached_modern_build(
        &queue_layout, 0U, NVX_GSP_INIT_ARGS_FLAG_STACK_IN_DMEM,
        &init_args) == NVX_GSP_INIT_ARGS_OK);
    assert(nvx_gsp_arguments_cached_modern_materialize(
        &init_args, init_args_bytes, NVX_GSP_INIT_ARGS_PAGE_SIZE) ==
        NVX_GSP_INIT_ARGS_OK);
    assert(nvx_dma_sync_init(&init_args_sync, &init_args_buffer) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_init(&shared_queue_sync, &shared_queue_buffer) == NVX_DMA_SYNC_OK);


    hardware.activate_gsp_on_start = 1U;
    assert(nvx_gsp_ga10x_execute_fwsec_frts(
        &vbios_buffer, &vbios_sync, &fwsec_info, 0U, &bar0,
        0x01000000ULL, 20U, 1U, fake_delay, &hardware,
        &executed_frts) == NVX_GSP_FALCON_OK);
    assert(executed_frts.frts_error_code == 0U);
    assert(executed_frts.wpr2_addr_lo_units == 0x1000U);
    assert(vbios_sync.owner == NVX_DMA_OWNER_CPU);
    assert(read_reg(&bar0, NVX_GSP_FALCON_BROM_ENGINE_MASK) ==
           NVX_GSP_VBIOS_FWSEC_ENGINE_MASK);
    assert(read_reg(&bar0, NVX_GSP_FALCON_BROM_PARAM_ADDR) == 0x700U);
    assert(read_reg(&bar0, NVX_GSP_FALCON_BASE + 0x100U) ==
           NVX_GSP_CPUCTL_HALTED);
    assert(vbios_bytes[0x2700U] == 0x5AU);
    frts_result = executed_frts;

    /* Invalid FWSEC evidence must be rejected before any DMA handoff. */
    frts_result.frts_error_code = 1U;
    assert(nvx_gsp_ga10x_prepare_riscv_after_frts(
        &memory, &init_args_buffer, &init_args_sync,
        &shared_queue_buffer, &shared_queue_sync, &queue_layout, &bar0, 0x01000000ULL,
        &frts_result, 20U, 1U, fake_delay, &hardware) ==
        NVX_GSP_FALCON_FWSEC_NOT_VERIFIED);
    assert(memory.radix3_elf_sync.owner == NVX_DMA_OWNER_CPU);
    assert(init_args_sync.owner == NVX_DMA_OWNER_CPU);
    assert(shared_queue_sync.owner == NVX_DMA_OWNER_CPU);
    frts_result.frts_error_code = 0U;

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

    /* End-to-end synthetic bootstrap: FRTS -> RISC-V setup -> SEC2 Booter Load -> active. */
    assert(nvx_mmio_write32(&bar0, NVX_GSP_RISCV_CPUCTL, 0U) == NVX_MMIO_OK);
    hardware.activate_gsp_after_sec2 = 1U;
    nvx_gsp_fwsec_result_snapshot_t bootstrap_frts = {0};
    assert(nvx_gsp_ga10x_bootstrap_traced(
        &vbios_buffer, &vbios_sync, &fwsec_info, 0U, &memory,
        &init_args_buffer, &init_args_sync, &shared_queue_buffer,
        &shared_queue_sync, &queue_layout, &bar0, 0x01000000ULL, 20U, 1U,
        fake_delay, &hardware, &bootstrap_frts, &mailbox1, &trace) ==
        NVX_GSP_FALCON_OK);
    assert(bootstrap_frts.frts_error_code == 0U);
    assert(trace.riscv_active == 1U);
    assert(trace.result == NVX_GSP_FALCON_OK);
    assert(memory.radix3_elf_sync.owner == NVX_DMA_OWNER_DEVICE);
    assert(init_args_sync.owner == NVX_DMA_OWNER_DEVICE);
    assert(shared_queue_sync.owner == NVX_DMA_OWNER_SHARED);
    assert(read_reg(&bar0, NVX_GSP_RISCV_BCR_CTRL) ==
           NVX_GSP_RISCV_BCR_CTRL_BOOT);
    /* The GSP may consume these buffers until reset proves quiescence. */
    assert(nvx_ga10x_falcon_reset(&bar0, NVX_GSP_FALCON_BASE, 20U, 1U,
        fake_delay, &hardware) == NVX_GSP_FALCON_OK);
    assert(nvx_dma_sync_for_cpu(&memory.radix3_elf_sync, 0U,
        memory.radix3_elf.size) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_for_cpu(&init_args_sync, 0U,
        init_args_buffer.size) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_for_cpu(&shared_queue_sync, 0U,
        shared_queue_buffer.size) == NVX_DMA_SYNC_OK);
    hardware.activate_gsp_after_sec2 = 0U;

    assert(nvx_dma_sync_destroy(&vbios_sync) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_destroy(&init_args_sync) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_destroy(&shared_queue_sync) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_destroy(&memory.radix3_elf_sync) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_destroy(&memory.booter_load_sync) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_destroy(&memory.wpr_metadata_sync) == NVX_DMA_SYNC_OK);
    free(metadata_bytes);
    free(radix_bytes);
    free(load_bytes);
    free(bar_bytes);
    return 0;
}
