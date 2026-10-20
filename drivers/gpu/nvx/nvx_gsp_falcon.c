/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_falcon.h"

#include <stdint.h>
#include <string.h>

#define NVX_GSP_PRIV_LOCKED_PATTERN 0xBADF4100U
#define NVX_GSP_PRIV_LOCKED_MASK   0xFFFFFF00U

static int nvx_gsp_ranges_overlap(const void *a, size_t a_size,
                                  const void *b, size_t b_size)
{
    uintptr_t a_start = (uintptr_t)a;
    uintptr_t b_start = (uintptr_t)b;

    if (a_size == 0U || b_size == 0U)
        return 0;
    if (a_start > UINTPTR_MAX - a_size || b_start > UINTPTR_MAX - b_size)
        return 1;
    return a_start < b_start + b_size && b_start < a_start + a_size;
}

nvx_gsp_falcon_status_t nvx_gsp_ga10x_inspect_falcon(
    const nvx_mmio_region_t *bar0,
    nvx_gsp_falcon_snapshot_t *out_snapshot)
{
    nvx_gsp_falcon_snapshot_t snapshot;
    nvx_mmio_status_t status;

    if (out_snapshot == 0 || bar0 == 0 || bar0->base == 0)
        return NVX_GSP_FALCON_INVALID_ARGUMENT;

    /* Do not clear an output object that aliases MMIO or its region descriptor. */
    if (nvx_gsp_ranges_overlap(out_snapshot, sizeof(*out_snapshot),
                              bar0, sizeof(*bar0)) ||
        nvx_gsp_ranges_overlap(out_snapshot, sizeof(*out_snapshot),
                              (const void *)bar0->base, bar0->length))
        return NVX_GSP_FALCON_INVALID_ARGUMENT;

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    memset(&snapshot, 0, sizeof(snapshot));

    if (bar0->length < NVX_GSP_FALCON_DIAGNOSTIC_BYTES)
        return NVX_GSP_FALCON_BAR_TOO_SMALL;

    /*
     * HWCFG2 is checked first. NVIDIA's published register documentation and
     * Linux Nova's independent implementation identify 0xBADF41xx as the
     * blocked PRIV-target read pattern. Do not write around this lock.
     */
    status = nvx_mmio_read32(bar0, NVX_GSP_FALCON_HWCFG2, &snapshot.hwcfg2);
    if (status != NVX_MMIO_OK)
        return NVX_GSP_FALCON_MMIO_ERROR;

    status = nvx_mmio_read32(bar0, NVX_GSP_FALCON_CPUCTL, &snapshot.cpuctl);
    if (status != NVX_MMIO_OK)
        return NVX_GSP_FALCON_MMIO_ERROR;

    status = nvx_mmio_read32(bar0, NVX_GSP_RISCV_CPUCTL,
                             &snapshot.riscv_cpuctl);
    if (status != NVX_MMIO_OK)
        return NVX_GSP_FALCON_MMIO_ERROR;

    status = nvx_mmio_read32(bar0, NVX_GSP_FALCON_MAILBOX0,
                             &snapshot.mailbox0);
    if (status != NVX_MMIO_OK)
        return NVX_GSP_FALCON_MMIO_ERROR;

    status = nvx_mmio_read32(bar0, NVX_GSP_FALCON_MAILBOX1,
                             &snapshot.mailbox1);
    if (status != NVX_MMIO_OK)
        return NVX_GSP_FALCON_MMIO_ERROR;

    if ((snapshot.hwcfg2 & NVX_GSP_PRIV_LOCKED_MASK) ==
        NVX_GSP_PRIV_LOCKED_PATTERN)
        return NVX_GSP_FALCON_PRIVILEGED_WINDOW_LOCKED;

    snapshot.reset_ready =
        (uint8_t)((snapshot.hwcfg2 & NVX_GSP_HWCFG2_RESET_READY) != 0U);
    snapshot.memory_scrubbing =
        (uint8_t)((snapshot.hwcfg2 & NVX_GSP_HWCFG2_MEMORY_SCRUBBING) != 0U);
    snapshot.riscv_present =
        (uint8_t)((snapshot.hwcfg2 & NVX_GSP_HWCFG2_RISCV_PRESENT) != 0U);
    snapshot.falcon_halted =
        (uint8_t)((snapshot.cpuctl & NVX_GSP_CPUCTL_HALTED) != 0U);
    snapshot.falcon_start_requested =
        (uint8_t)((snapshot.cpuctl & NVX_GSP_CPUCTL_START) != 0U);
    snapshot.riscv_active =
        (uint8_t)((snapshot.riscv_cpuctl & NVX_GSP_RISCV_ACTIVE) != 0U);
    snapshot.riscv_halted =
        (uint8_t)((snapshot.riscv_cpuctl & NVX_GSP_RISCV_HALTED) != 0U);
    snapshot.mailbox_suspended =
        (uint8_t)((snapshot.mailbox0 & NVX_GSP_MAILBOX0_SUSPENDED) != 0U);
    snapshot.snapshot_valid = 1U;
    *out_snapshot = snapshot;
    return NVX_GSP_FALCON_OK;
}

nvx_gsp_falcon_status_t nvx_gsp_ga10x_wait_riscv_active(
    const nvx_mmio_region_t *bar0, uint32_t timeout_ms,
    uint32_t poll_interval_ms, nvx_gsp_delay_ms_fn delay_ms, void *context)
{
    uint32_t elapsed_ms = 0U;

    if (bar0 == 0 || bar0->base == 0 || delay_ms == 0 ||
        timeout_ms == 0U || poll_interval_ms == 0U)
        return NVX_GSP_FALCON_INVALID_ARGUMENT;
    if (bar0->length < NVX_GSP_FALCON_DIAGNOSTIC_BYTES)
        return NVX_GSP_FALCON_BAR_TOO_SMALL;

    for (;;) {
        uint32_t hwcfg2 = 0U;
        uint32_t riscv_cpuctl = 0U;
        nvx_mmio_status_t status;

        status = nvx_mmio_read32(bar0, NVX_GSP_FALCON_HWCFG2, &hwcfg2);
        if (status != NVX_MMIO_OK)
            return NVX_GSP_FALCON_MMIO_ERROR;
        if ((hwcfg2 & NVX_GSP_PRIV_LOCKED_MASK) ==
            NVX_GSP_PRIV_LOCKED_PATTERN)
            return NVX_GSP_FALCON_PRIVILEGED_WINDOW_LOCKED;

        status = nvx_mmio_read32(bar0, NVX_GSP_RISCV_CPUCTL,
                                 &riscv_cpuctl);
        if (status != NVX_MMIO_OK)
            return NVX_GSP_FALCON_MMIO_ERROR;
        if ((riscv_cpuctl & NVX_GSP_RISCV_ACTIVE) != 0U)
            return NVX_GSP_FALCON_OK;

        if (elapsed_ms >= timeout_ms)
            return NVX_GSP_FALCON_RISCV_TIMEOUT;

        uint32_t remaining_ms = timeout_ms - elapsed_ms;
        uint32_t sleep_ms = poll_interval_ms < remaining_ms ?
                            poll_interval_ms : remaining_ms;
        delay_ms(sleep_ms, context);
        elapsed_ms += sleep_ms;
    }
}

/*
 * GA102+ BROM programming order cross-checked against Linux Nova's
 * ga102 Falcon HAL and NVIDIA's published dev_falcon_v4.h:
 * parameter address, engine mask, ucode ID, then RSA-3K selector.
 */

static int nvx_ga10x_falcon_pair_valid(uint32_t falcon_base,
                                       uint32_t falcon2_base,
                                       uint32_t engine_id_mask)
{
    return (falcon_base == NVX_GSP_FALCON_BASE &&
            falcon2_base == NVX_GSP_FALCON2_BASE &&
            engine_id_mask == 0x0400U) ||
           (falcon_base == NVX_SEC2_FALCON_BASE &&
            falcon2_base == NVX_SEC2_FALCON2_BASE &&
            engine_id_mask == 0x0001U);
}

nvx_gsp_falcon_status_t nvx_ga10x_falcon_program_brom(
    const nvx_mmio_region_t *bar0, uint32_t falcon_base,
    uint32_t falcon2_base, uint32_t engine_id_mask,
    uint32_t ucode_id, uint32_t pkc_data_offset)
{
    uint32_t hwcfg2 = 0U;
    uint32_t cpuctl = 0U;
    size_t required_bytes;

    if (bar0 == 0 || bar0->base == 0)
        return NVX_GSP_FALCON_INVALID_ARGUMENT;
    if (!nvx_ga10x_falcon_pair_valid(falcon_base, falcon2_base,
                                    engine_id_mask))
        return NVX_GSP_FALCON_BROM_INVALID_PARAMETERS;
    required_bytes = (size_t)NVX_GA10X_FALCON_BROM_PARAM_ADDR(falcon2_base) + 4U;
    if (bar0->length < required_bytes)
        return NVX_GSP_FALCON_BAR_TOO_SMALL;
    if (ucode_id == 0U || ucode_id > NVX_GSP_FALCON_BROM_UCODE_COUNT ||
        (pkc_data_offset & 3U) != 0U)
        return NVX_GSP_FALCON_BROM_INVALID_PARAMETERS;

    if (nvx_mmio_read32(bar0, falcon_base + 0x0F4U, &hwcfg2) != NVX_MMIO_OK ||
        nvx_mmio_read32(bar0, falcon_base + 0x100U, &cpuctl) != NVX_MMIO_OK)
        return NVX_GSP_FALCON_MMIO_ERROR;
    if ((hwcfg2 & NVX_GSP_PRIV_LOCKED_MASK) == NVX_GSP_PRIV_LOCKED_PATTERN)
        return NVX_GSP_FALCON_PRIVILEGED_WINDOW_LOCKED;
    if ((cpuctl & NVX_GSP_CPUCTL_HALTED) == 0U)
        return NVX_GSP_FALCON_NOT_HALTED;

    /* GA102 OpenRM/Nova order: parameter address, engine mask, ucode ID,
       and RSA-3K selector. Validate all bounds before the first write. */
    if (nvx_mmio_write32(bar0, NVX_GA10X_FALCON_BROM_PARAM_ADDR(falcon2_base),
                         pkc_data_offset) != NVX_MMIO_OK ||
        nvx_mmio_write32(bar0, NVX_GA10X_FALCON_BROM_ENGINE_MASK(falcon2_base),
                         engine_id_mask) != NVX_MMIO_OK ||
        nvx_mmio_write32(bar0, NVX_GA10X_FALCON_BROM_UCODE_ID(falcon2_base),
                         ucode_id) != NVX_MMIO_OK ||
        nvx_mmio_write32(bar0, NVX_GA10X_FALCON_BROM_MOD_SEL(falcon2_base),
                         NVX_GSP_FALCON_BROM_RSA3K) != NVX_MMIO_OK)
        return NVX_GSP_FALCON_MMIO_ERROR;
    return NVX_GSP_FALCON_OK;
}

nvx_gsp_falcon_status_t nvx_gsp_ga10x_program_brom(
    const nvx_mmio_region_t *bar0, uint32_t engine_id_mask,
    uint32_t ucode_id, uint32_t pkc_data_offset)
{
    return nvx_ga10x_falcon_program_brom(bar0, NVX_GSP_FALCON_BASE,
        NVX_GSP_FALCON2_BASE, engine_id_mask, ucode_id, pkc_data_offset);
}

nvx_gsp_falcon_status_t nvx_ga10x_falcon_start(
    const nvx_mmio_region_t *bar0, uint32_t falcon_base,
    uint32_t boot_vector)
{
    uint32_t hwcfg2 = 0U;
    uint32_t cpuctl = 0U;

    if (bar0 == 0 || bar0->base == 0)
        return NVX_GSP_FALCON_INVALID_ARGUMENT;
    if (falcon_base != NVX_GSP_FALCON_BASE &&
        falcon_base != NVX_SEC2_FALCON_BASE)
        return NVX_GSP_FALCON_BROM_INVALID_PARAMETERS;
    if (bar0->length < (size_t)(falcon_base + 0x108U))
        return NVX_GSP_FALCON_BAR_TOO_SMALL;
    if ((boot_vector & 3U) != 0U)
        return NVX_GSP_FALCON_BOOT_VECTOR_INVALID;

    if (nvx_mmio_read32(bar0, falcon_base + 0x0F4U, &hwcfg2) != NVX_MMIO_OK ||
        nvx_mmio_read32(bar0, falcon_base + 0x100U, &cpuctl) != NVX_MMIO_OK)
        return NVX_GSP_FALCON_MMIO_ERROR;
    if ((hwcfg2 & NVX_GSP_PRIV_LOCKED_MASK) == NVX_GSP_PRIV_LOCKED_PATTERN)
        return NVX_GSP_FALCON_PRIVILEGED_WINDOW_LOCKED;
    if ((cpuctl & NVX_GSP_CPUCTL_HALTED) == 0U)
        return NVX_GSP_FALCON_NOT_HALTED;
    if (nvx_mmio_write32(bar0, falcon_base + 0x104U,
                         boot_vector) != NVX_MMIO_OK ||
        nvx_mmio_write32(bar0, falcon_base + 0x100U,
                         NVX_GSP_CPUCTL_START) != NVX_MMIO_OK)
        return NVX_GSP_FALCON_MMIO_ERROR;
    return NVX_GSP_FALCON_OK;
}

nvx_gsp_falcon_status_t nvx_gsp_ga10x_start_falcon(
    const nvx_mmio_region_t *bar0, uint32_t boot_vector)
{
    return nvx_ga10x_falcon_start(bar0, NVX_GSP_FALCON_BASE, boot_vector);
}

static int nvx_ga10x_falcon_base_valid(uint32_t base)
{
    return base == NVX_GSP_FALCON_BASE || base == NVX_SEC2_FALCON_BASE;
}

static nvx_gsp_falcon_status_t nvx_ga10x_falcon_dma_wait(
    const nvx_mmio_region_t *bar0, uint32_t command_register,
    uint32_t mask, uint32_t expected, uint32_t timeout_ms,
    uint32_t poll_interval_ms, nvx_gsp_delay_ms_fn delay_ms, void *context)
{
    uint32_t elapsed_ms = 0U;
    for (;;) {
        uint32_t command = 0U;
        if (nvx_mmio_read32(bar0, command_register, &command) != NVX_MMIO_OK)
            return NVX_GSP_FALCON_MMIO_ERROR;
        if ((command & mask) == expected)
            return NVX_GSP_FALCON_OK;
        if (elapsed_ms >= timeout_ms)
            return NVX_GSP_FALCON_DMA_TIMEOUT;
        uint32_t remaining = timeout_ms - elapsed_ms;
        uint32_t sleep_ms = poll_interval_ms < remaining ?
                            poll_interval_ms : remaining;
        delay_ms(sleep_ms, context);
        elapsed_ms += sleep_ms;
    }
}

nvx_gsp_falcon_status_t nvx_ga10x_falcon_dma_upload(
    const nvx_mmio_region_t *bar0, uint32_t falcon_base,
    uint64_t source_phys, uint32_t source_offset,
    uint32_t destination_offset, uint32_t length,
    nvx_ga10x_falcon_memory_t target, uint8_t set_dm_tag,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context)
{
    uint32_t hwcfg2 = 0U;
    uint32_t cpuctl = 0U;
    uint32_t command_register;
    uint32_t command;
    uint64_t source_end;
    uint64_t destination_end;
    uint32_t target_bits;

    if (!bar0 || !bar0->base || !delay_ms || timeout_ms == 0U ||
        poll_interval_ms == 0U)
        return NVX_GSP_FALCON_INVALID_ARGUMENT;
    if (!nvx_ga10x_falcon_base_valid(falcon_base))
        return NVX_GSP_FALCON_DMA_INVALID_TRANSFER;
    if (bar0->length < (size_t)(falcon_base + 0x628U))
        return NVX_GSP_FALCON_BAR_TOO_SMALL;
    if (source_phys == 0U || (source_phys & 0xFFU) != 0U ||
        (source_phys >> 49U) != 0U || length == 0U ||
        (source_offset & 0xFFU) != 0U ||
        (destination_offset & 0xFFU) != 0U ||
        (length % NVX_GA10X_FALCON_DMA_CHUNK_BYTES) != 0U ||
        source_offset > UINT32_MAX - length ||
        destination_offset > 0x01000000U - length ||
        (target != NVX_GA10X_FALCON_SECURE_IMEM &&
         target != NVX_GA10X_FALCON_NONSECURE_IMEM &&
         target != NVX_GA10X_FALCON_DMEM) ||
        (set_dm_tag != 0U && target != NVX_GA10X_FALCON_DMEM))
        return NVX_GSP_FALCON_DMA_INVALID_TRANSFER;
    source_end = source_phys + (uint64_t)source_offset + length;
    destination_end = (uint64_t)destination_offset + length;
    if (source_end < source_phys || destination_end > 0x01000000ULL)
        return NVX_GSP_FALCON_DMA_INVALID_TRANSFER;

    command_register = NVX_GA10X_FALCON_DMATRFCMD(falcon_base);
    nvx_gsp_falcon_status_t status = nvx_ga10x_falcon_dma_wait(
        bar0, command_register, NVX_GA10X_FALCON_DMA_FULL, 0U,
        timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK)
        return status;

    if (nvx_mmio_read32(bar0, falcon_base + 0x0F4U, &hwcfg2) != NVX_MMIO_OK ||
        nvx_mmio_read32(bar0, falcon_base + 0x100U, &cpuctl) != NVX_MMIO_OK)
        return NVX_GSP_FALCON_MMIO_ERROR;
    if ((hwcfg2 & NVX_GSP_PRIV_LOCKED_MASK) == NVX_GSP_PRIV_LOCKED_PATTERN)
        return NVX_GSP_FALCON_PRIVILEGED_WINDOW_LOCKED;
    if ((cpuctl & NVX_GSP_CPUCTL_HALTED) == 0U)
        return NVX_GSP_FALCON_NOT_HALTED;

    if (nvx_mmio_write32(bar0, NVX_GA10X_FALCON_FBIF_CTL(falcon_base),
                         NVX_GA10X_FALCON_DMA_ALLOW_PHYS) != NVX_MMIO_OK ||
        nvx_mmio_write32(bar0, NVX_GA10X_FALCON_FBIF_TRANSCFG0(falcon_base),
                         NVX_GA10X_FALCON_DMA_COHERENT_PHYS) != NVX_MMIO_OK ||
        nvx_mmio_write32(bar0, NVX_GA10X_FALCON_DMACTL(falcon_base), 0U) !=
                         NVX_MMIO_OK ||
        nvx_mmio_write32(bar0, NVX_GA10X_FALCON_DMATRFBASE(falcon_base),
                         (uint32_t)(source_phys >> 8U)) != NVX_MMIO_OK ||
        nvx_mmio_write32(bar0, NVX_GA10X_FALCON_DMATRFBASE1(falcon_base),
                         (uint32_t)((source_phys >> 40U) & 0x1FFU)) !=
                         NVX_MMIO_OK)
        return NVX_GSP_FALCON_MMIO_ERROR;

    target_bits = target == NVX_GA10X_FALCON_DMEM ? 0U :
                  NVX_GA10X_FALCON_DMA_IMEM;
    if (target == NVX_GA10X_FALCON_SECURE_IMEM)
        target_bits |= (1U << 2U);
    if (set_dm_tag)
        target_bits |= NVX_GA10X_FALCON_DMA_SET_DMTAG;
    command = NVX_GA10X_FALCON_DMA_SIZE_256B | target_bits;

    for (uint32_t done = 0U; done < length;
         done += NVX_GA10X_FALCON_DMA_CHUNK_BYTES) {
        status = nvx_ga10x_falcon_dma_wait(
            bar0, command_register, NVX_GA10X_FALCON_DMA_FULL, 0U,
            timeout_ms, poll_interval_ms, delay_ms, context);
        if (status != NVX_GSP_FALCON_OK)
            return status;
        if (nvx_mmio_write32(bar0,
                NVX_GA10X_FALCON_DMATRFMOFFS(falcon_base),
                destination_offset + done) != NVX_MMIO_OK ||
            nvx_mmio_write32(bar0,
                NVX_GA10X_FALCON_DMATRFFBOFFS(falcon_base),
                source_offset + done) != NVX_MMIO_OK ||
            nvx_mmio_write32(bar0, command_register, command) != NVX_MMIO_OK)
            return NVX_GSP_FALCON_MMIO_ERROR;
    }

    return nvx_ga10x_falcon_dma_wait(
        bar0, command_register, NVX_GA10X_FALCON_DMA_IDLE,
        NVX_GA10X_FALCON_DMA_IDLE, timeout_ms, poll_interval_ms,
        delay_ms, context);
}
