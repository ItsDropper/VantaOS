/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
/* HOST TEST, synthetic MMIO. Not hardware evidence. */
#include "nvx_gsp_quiesce.h"

#include <assert.h>
#include <string.h>

static uint8_t bar[NVX_GA10X_BAR0_REQUIRED_BYTES + 64U]
    __attribute__((aligned(4)));
static uint32_t delays;
static uint32_t clear_scrub_after;

static void put32(size_t off, uint32_t v) { memcpy(bar + off, &v, 4); }
static uint32_t get32(size_t off) { uint32_t v; memcpy(&v, bar + off, 4); return v; }

static void delay(uint32_t ms, void *ctx)
{
    (void)ctx;
    assert(ms != 0U);
    ++delays;
    if (clear_scrub_after && delays >= clear_scrub_after) {
        put32(NVX_GSP_FALCON_BASE + 0xF4U,
              get32(NVX_GSP_FALCON_BASE + 0xF4U) & ~NVX_GSP_HWCFG2_MEMORY_SCRUBBING);
        put32(NVX_SEC2_FALCON_BASE + 0xF4U,
              get32(NVX_SEC2_FALCON_BASE + 0xF4U) & ~NVX_GSP_HWCFG2_MEMORY_SCRUBBING);
    }
}

static nvx_attached_device_t dev;
static nvx_gsp_quiesce_context_t ctx;
static nvx_gsp_startup_result_t startup;

static void reset_world(void)
{
    memset(bar, 0, sizeof(bar));
    memset(&dev, 0, sizeof(dev));
    memset(&ctx, 0, sizeof(ctx));
    delays = 0U;
    clear_scrub_after = 0U;
    dev.attached = 1U;
    dev.bar0_mapping = bar;
    dev.bar0.size = sizeof(bar);
    dev.bar0.size_known = 1U;
    ctx.delay_ms = delay;
    ctx.timeout_ms = 20U;
    ctx.poll_interval_ms = 2U;
}

int main(void)
{
    /* Success: both cores idle after reset; evidence recorded. */
    reset_world();
    put32(NVX_GSP_FALCON_BASE + 0xF4U, 0x00000400U);
    assert(nvx_gsp_ga10x_quiesce(&dev, &startup, &ctx) == 1);
    assert(ctx.gsp.result == NVX_GSP_QUIESCE_OK && ctx.sec2.result == NVX_GSP_QUIESCE_OK);
    assert(ctx.gsp.attempted && ctx.sec2.attempted);
    assert(ctx.gsp.hwcfg2_before == 0x400U);
    assert(get32(NVX_GA10X_FALCON_ENGINE(NVX_GSP_FALCON_BASE)) == 0U);

    /* Scrub completes only after some delays: still succeeds within timeout. */
    reset_world();
    put32(NVX_GSP_FALCON_BASE + 0xF4U, NVX_GSP_HWCFG2_MEMORY_SCRUBBING);
    put32(NVX_SEC2_FALCON_BASE + 0xF4U, NVX_GSP_HWCFG2_MEMORY_SCRUBBING);
    clear_scrub_after = 3U;
    assert(nvx_gsp_ga10x_quiesce(&dev, &startup, &ctx) == 1);

    /* Scrub never completes: reset fails, not quiesced, SEC2 still attempted. */
    reset_world();
    put32(NVX_GSP_FALCON_BASE + 0xF4U, NVX_GSP_HWCFG2_MEMORY_SCRUBBING);
    assert(nvx_gsp_ga10x_quiesce(&dev, &startup, &ctx) == 0);
    assert(ctx.gsp.result == NVX_GSP_QUIESCE_RESET_FAILED);
    assert(ctx.gsp.reset_status == NVX_GSP_FALCON_RESET_TIMEOUT);
    assert(ctx.sec2.attempted);

    /* GSP RISC-V still reports active after reset: refuse to claim quiescence. */
    reset_world();
    put32(NVX_GSP_RISCV_CPUCTL, NVX_GSP_RISCV_ACTIVE);
    assert(nvx_gsp_ga10x_quiesce(&dev, &startup, &ctx) == 0);
    assert(ctx.gsp.result == NVX_GSP_QUIESCE_STILL_RUNNING);

    /* SEC2 start bit persists after reset. */
    reset_world();
    put32(NVX_SEC2_FALCON_BASE + 0x100U, NVX_GSP_CPUCTL_START);
    assert(nvx_gsp_ga10x_quiesce(&dev, &startup, &ctx) == 0);
    assert(ctx.gsp.result == NVX_GSP_QUIESCE_OK);
    assert(ctx.sec2.result == NVX_GSP_QUIESCE_STILL_RUNNING);

    /* Device loss (all ones) is not proof of quiescence. */
    reset_world();
    put32(NVX_GSP_FALCON_BASE + 0xF4U, 0xFFFFFFFFU);
    assert(nvx_gsp_ga10x_quiesce(&dev, &startup, &ctx) == 0);
    assert(ctx.gsp.result == NVX_GSP_QUIESCE_DEVICE_LOST);
    assert(get32(NVX_GA10X_FALCON_ENGINE(NVX_GSP_FALCON_BASE)) == 0U); /* no reset written */

    /* PRIV-locked window. */
    reset_world();
    put32(NVX_GSP_FALCON_BASE + 0xF4U, 0xBADF4100U);
    assert(nvx_gsp_ga10x_quiesce(&dev, &startup, &ctx) == 0);
    assert(ctx.gsp.result == NVX_GSP_QUIESCE_PRIV_LOCKED);

    /* Argument and BAR validation. */
    reset_world();
    assert(nvx_gsp_ga10x_quiesce(&dev, &startup, 0) == 0);
    assert(nvx_gsp_ga10x_quiesce(0, &startup, &ctx) == 0);
    assert(ctx.gsp.result == NVX_GSP_QUIESCE_INVALID_ARGUMENT);
    reset_world(); ctx.delay_ms = 0;
    assert(nvx_gsp_ga10x_quiesce(&dev, &startup, &ctx) == 0);
    reset_world(); ctx.timeout_ms = 0U;
    assert(nvx_gsp_ga10x_quiesce(&dev, &startup, &ctx) == 0);
    reset_world(); dev.attached = 0U;
    assert(nvx_gsp_ga10x_quiesce(&dev, &startup, &ctx) == 0);
    reset_world(); dev.bar0.size = 0x1000U;
    assert(nvx_gsp_ga10x_quiesce(&dev, &startup, &ctx) == 0);
    assert(ctx.gsp.result == NVX_GSP_QUIESCE_BAR_TOO_SMALL);
    reset_world(); dev.bar0.size_known = 0U;
    assert(nvx_gsp_ga10x_quiesce(&dev, &startup, &ctx) == 0);
    return 0;
}
