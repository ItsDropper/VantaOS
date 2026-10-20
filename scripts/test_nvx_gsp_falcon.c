/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_falcon.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint8_t bar[NVX_GSP_FALCON_DIAGNOSTIC_BYTES]
    __attribute__((aligned(4)));

static void put32(size_t offset, uint32_t value)
{
    memcpy(bar + offset, &value, sizeof(value));
}

static unsigned delay_calls;
static unsigned activate_after;

static void fake_delay(uint32_t milliseconds, void *context)
{
    uint32_t *elapsed = (uint32_t *)context;
    assert(milliseconds != 0U);
    *elapsed += milliseconds;
    ++delay_calls;
    if (delay_calls >= activate_after)
        put32(NVX_GSP_RISCV_CPUCTL, NVX_GSP_RISCV_ACTIVE);
}

int main(void)
{
    nvx_mmio_region_t region = {bar, sizeof(bar)};
    nvx_gsp_falcon_snapshot_t snapshot;

    memset(bar, 0, sizeof(bar));
    memset(&snapshot, 0xA5, sizeof(snapshot));
    assert(nvx_gsp_ga10x_inspect_falcon(&region, &snapshot) ==
           NVX_GSP_FALCON_OK);
    assert(snapshot.snapshot_valid);
    assert(!snapshot.reset_ready && !snapshot.memory_scrubbing);
    assert(!snapshot.riscv_present && !snapshot.falcon_halted);
    assert(!snapshot.falcon_start_requested && !snapshot.riscv_active);
    assert(!snapshot.riscv_halted && !snapshot.mailbox_suspended);

    put32(NVX_GSP_FALCON_HWCFG2, NVX_GSP_HWCFG2_RESET_READY |
          NVX_GSP_HWCFG2_RISCV_PRESENT);
    put32(NVX_GSP_FALCON_CPUCTL, NVX_GSP_CPUCTL_START);
    put32(NVX_GSP_RISCV_CPUCTL, NVX_GSP_RISCV_ACTIVE);
    put32(NVX_GSP_FALCON_MAILBOX0, NVX_GSP_MAILBOX0_SUSPENDED);
    put32(NVX_GSP_FALCON_MAILBOX1, 0x12345678U);
    assert(nvx_gsp_ga10x_inspect_falcon(&region, &snapshot) ==
           NVX_GSP_FALCON_OK);
    assert(snapshot.reset_ready && snapshot.riscv_present);
    assert(snapshot.falcon_start_requested && snapshot.riscv_active);
    assert(snapshot.mailbox_suspended);
    assert(snapshot.mailbox1 == 0x12345678U);

    put32(NVX_GSP_FALCON_HWCFG2, NVX_GSP_HWCFG2_MEMORY_SCRUBBING);
    put32(NVX_GSP_FALCON_CPUCTL, NVX_GSP_CPUCTL_HALTED);
    put32(NVX_GSP_RISCV_CPUCTL, NVX_GSP_RISCV_HALTED);
    put32(NVX_GSP_FALCON_MAILBOX0, 0U);
    assert(nvx_gsp_ga10x_inspect_falcon(&region, &snapshot) ==
           NVX_GSP_FALCON_OK);
    assert(snapshot.memory_scrubbing && snapshot.falcon_halted);
    assert(snapshot.riscv_halted && !snapshot.riscv_active);

    uint32_t elapsed_ms = 0U;
    delay_calls = 0U;
    activate_after = 2U;
    put32(NVX_GSP_FALCON_HWCFG2, NVX_GSP_HWCFG2_RESET_READY);
    put32(NVX_GSP_RISCV_CPUCTL, 0U);
    assert(nvx_gsp_ga10x_wait_riscv_active(&region, 50U, 10U,
           fake_delay, &elapsed_ms) == NVX_GSP_FALCON_OK);
    assert(delay_calls == 2U && elapsed_ms == 20U);

    elapsed_ms = 0U;
    delay_calls = 0U;
    activate_after = 99U;
    put32(NVX_GSP_RISCV_CPUCTL, 0U);
    assert(nvx_gsp_ga10x_wait_riscv_active(&region, 25U, 10U,
           fake_delay, &elapsed_ms) == NVX_GSP_FALCON_RISCV_TIMEOUT);
    assert(delay_calls == 3U && elapsed_ms == 25U);

    assert(nvx_gsp_ga10x_wait_riscv_active(&region, 0U, 10U,
           fake_delay, &elapsed_ms) == NVX_GSP_FALCON_INVALID_ARGUMENT);
    put32(NVX_GSP_FALCON_HWCFG2, 0xBADF41A5U);
    memset(&snapshot, 0xA5, sizeof(snapshot));
    assert(nvx_gsp_ga10x_inspect_falcon(&region, &snapshot) ==
           NVX_GSP_FALCON_PRIVILEGED_WINDOW_LOCKED);
    assert(snapshot.snapshot_valid == 0U);

    nvx_mmio_region_t short_region = {bar,
        NVX_GSP_FALCON_DIAGNOSTIC_BYTES - 1U};
    memset(&snapshot, 0xA5, sizeof(snapshot));
    assert(nvx_gsp_ga10x_inspect_falcon(&short_region, &snapshot) ==
           NVX_GSP_FALCON_BAR_TOO_SMALL);
    assert(snapshot.snapshot_valid == 0U);
    assert(nvx_gsp_ga10x_inspect_falcon(0, &snapshot) ==
           NVX_GSP_FALCON_INVALID_ARGUMENT);
    assert(nvx_gsp_ga10x_inspect_falcon(&region, 0) ==
           NVX_GSP_FALCON_INVALID_ARGUMENT);
    assert(nvx_gsp_ga10x_inspect_falcon(&region,
           (nvx_gsp_falcon_snapshot_t *)(void *)bar) ==
           NVX_GSP_FALCON_INVALID_ARGUMENT);
    return 0;
}
