/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_quiesce.h"

#include <string.h>

#define QUIESCE_PRIV_LOCKED_PATTERN 0xBADF4100U
#define QUIESCE_PRIV_LOCKED_MASK    0xFFFFFF00U
#define QUIESCE_ALL_ONES            0xFFFFFFFFU
#define QUIESCE_HWCFG2_OFFSET       0x0F4U

static int read32(const nvx_mmio_region_t *bar0, uint32_t offset, uint32_t *out)
{
    return nvx_mmio_read32(bar0, offset, out) == NVX_MMIO_OK;
}

/*
 * Reset one falcon and verify through readback that its core is not running.
 * riscv_cpuctl_offset == 0 means the engine has no RISC-V core to check (SEC2
 * on GA10x is checked through its falcon CPUCTL only).
 */
static nvx_gsp_quiesce_result_t quiesce_engine(
    const nvx_mmio_region_t *bar0, uint32_t falcon_base,
    uint32_t riscv_cpuctl_offset, const nvx_gsp_quiesce_context_t *ctx,
    nvx_gsp_quiesce_engine_report_t *report)
{
    report->attempted = 1U;

    if (!read32(bar0, falcon_base + QUIESCE_HWCFG2_OFFSET,
                &report->hwcfg2_before))
        return report->result = NVX_GSP_QUIESCE_MMIO_ERROR;
    if (report->hwcfg2_before == QUIESCE_ALL_ONES)
        return report->result = NVX_GSP_QUIESCE_DEVICE_LOST;
    if ((report->hwcfg2_before & QUIESCE_PRIV_LOCKED_MASK) ==
        QUIESCE_PRIV_LOCKED_PATTERN)
        return report->result = NVX_GSP_QUIESCE_PRIV_LOCKED;

    /* Evidence only; these reads do not influence the decision to reset. */
    (void)read32(bar0, falcon_base + 0x100U, &report->cpuctl_before);
    if (riscv_cpuctl_offset)
        (void)read32(bar0, riscv_cpuctl_offset, &report->riscv_cpuctl_before);

    report->reset_status = nvx_ga10x_falcon_reset(
        bar0, falcon_base, ctx->timeout_ms, ctx->poll_interval_ms,
        ctx->delay_ms, ctx->delay_context);
    if (report->reset_status == NVX_GSP_FALCON_PRIVILEGED_WINDOW_LOCKED)
        return report->result = NVX_GSP_QUIESCE_PRIV_LOCKED;
    if (report->reset_status != NVX_GSP_FALCON_OK)
        return report->result = NVX_GSP_QUIESCE_RESET_FAILED;

    if (!read32(bar0, falcon_base + QUIESCE_HWCFG2_OFFSET,
                &report->hwcfg2_after) ||
        !read32(bar0, falcon_base + 0x100U, &report->cpuctl_after))
        return report->result = NVX_GSP_QUIESCE_MMIO_ERROR;
    if (report->hwcfg2_after == QUIESCE_ALL_ONES)
        return report->result = NVX_GSP_QUIESCE_DEVICE_LOST;
    if (riscv_cpuctl_offset &&
        !read32(bar0, riscv_cpuctl_offset, &report->riscv_cpuctl_after))
        return report->result = NVX_GSP_QUIESCE_MMIO_ERROR;

    /* Post-reset: nothing may be started, and no RISC-V core may be active. */
    if ((report->cpuctl_after & NVX_GSP_CPUCTL_START) != 0U)
        return report->result = NVX_GSP_QUIESCE_STILL_RUNNING;
    if (riscv_cpuctl_offset &&
        (report->riscv_cpuctl_after & NVX_GSP_RISCV_ACTIVE) != 0U)
        return report->result = NVX_GSP_QUIESCE_STILL_RUNNING;

    return report->result = NVX_GSP_QUIESCE_OK;
}

int nvx_gsp_ga10x_quiesce(
    nvx_attached_device_t *device,
    const nvx_gsp_startup_result_t *startup_result,
    void *context)
{
    nvx_gsp_quiesce_context_t *ctx = (nvx_gsp_quiesce_context_t *)context;
    nvx_mmio_region_t bar0;

    (void)startup_result; /* Reset is unconditional; no result field is trusted. */

    if (!ctx)
        return 0;
    memset(&ctx->gsp, 0, sizeof(ctx->gsp));
    memset(&ctx->sec2, 0, sizeof(ctx->sec2));

    if (!device || !device->attached || !device->bar0_mapping ||
        !ctx->delay_ms || ctx->timeout_ms == 0U ||
        ctx->poll_interval_ms == 0U) {
        ctx->gsp.result = ctx->sec2.result = NVX_GSP_QUIESCE_INVALID_ARGUMENT;
        return 0;
    }
    if (!device->bar0.size_known ||
        device->bar0.size < NVX_GA10X_BAR0_REQUIRED_BYTES) {
        ctx->gsp.result = ctx->sec2.result = NVX_GSP_QUIESCE_BAR_TOO_SMALL;
        return 0;
    }

    bar0.base = (volatile uint8_t *)device->bar0_mapping;
    bar0.length = (size_t)device->bar0.size;

    /* GSP first: it is the RM-facing engine that can drive further channels. */
    if (quiesce_engine(&bar0, NVX_GSP_FALCON_BASE, NVX_GSP_RISCV_CPUCTL,
                       ctx, &ctx->gsp) != NVX_GSP_QUIESCE_OK) {
        /* Still attempt SEC2 for evidence, but the overall result is failure. */
        (void)quiesce_engine(&bar0, NVX_SEC2_FALCON_BASE, 0U, ctx, &ctx->sec2);
        return 0;
    }
    if (quiesce_engine(&bar0, NVX_SEC2_FALCON_BASE, 0U, ctx, &ctx->sec2) !=
        NVX_GSP_QUIESCE_OK)
        return 0;
    return 1;
}
