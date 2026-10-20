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
