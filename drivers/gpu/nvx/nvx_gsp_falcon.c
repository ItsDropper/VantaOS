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
