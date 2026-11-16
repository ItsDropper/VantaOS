/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
/* HOST TEST, synthetic MMIO. Covers the read-only handoff guard only. */
#include "nvx_gsp_adapter.h"

#include <assert.h>
#include <string.h>

static uint8_t bar[NVX_GA10X_BAR0_REQUIRED_BYTES] __attribute__((aligned(4)));
static void put32(size_t o, uint32_t v) { memcpy(bar + o, &v, 4); }
static nvx_mmio_region_t region(void) { nvx_mmio_region_t r = { bar, sizeof(bar) }; return r; }

int main(void)
{
    nvx_gsp_handoff_report_t rep;
    nvx_mmio_region_t r = region();

    memset(bar, 0, sizeof(bar));
    assert(nvx_gsp_adapter_check_handoff(&r, 0x0002U, &rep) == NVX_GSP_HANDOFF_CLEAR);
    assert(rep.pci_command == 0x0002U);

    /* Bus mastering already on: unknown DMA owner. */
    assert(nvx_gsp_adapter_check_handoff(&r, 0x0002U | 0x0004U, &rep) ==
           NVX_GSP_HANDOFF_BUS_MASTER_ACTIVE);
    assert(rep.result == NVX_GSP_HANDOFF_BUS_MASTER_ACTIVE);

    /* GSP RISC-V already running. */
    put32(NVX_GSP_RISCV_CPUCTL, NVX_GSP_RISCV_ACTIVE);
    assert(nvx_gsp_adapter_check_handoff(&r, 0U, &rep) == NVX_GSP_HANDOFF_FIRMWARE_ACTIVE);
    assert(rep.riscv_cpuctl == NVX_GSP_RISCV_ACTIVE);
    put32(NVX_GSP_RISCV_CPUCTL, 0U);

    /* Falcon / SEC2 start bits. */
    put32(NVX_GSP_FALCON_CPUCTL, NVX_GSP_CPUCTL_START);
    assert(nvx_gsp_adapter_check_handoff(&r, 0U, &rep) == NVX_GSP_HANDOFF_FIRMWARE_ACTIVE);
    put32(NVX_GSP_FALCON_CPUCTL, 0U);
    put32(NVX_SEC2_FALCON_BASE + 0x100U, NVX_GSP_CPUCTL_START);
    assert(nvx_gsp_adapter_check_handoff(&r, 0U, &rep) == NVX_GSP_HANDOFF_FIRMWARE_ACTIVE);
    put32(NVX_SEC2_FALCON_BASE + 0x100U, 0U);

    /* Device lost. */
    put32(NVX_GSP_FALCON_HWCFG2, 0xFFFFFFFFU);
    assert(nvx_gsp_adapter_check_handoff(&r, 0U, &rep) == NVX_GSP_HANDOFF_DEVICE_LOST);
    put32(NVX_GSP_FALCON_HWCFG2, 0U);

    /* Argument validation; output always written when provided. */
    assert(nvx_gsp_adapter_check_handoff(&r, 0U, 0) == NVX_GSP_HANDOFF_INVALID_ARGUMENT);
    assert(nvx_gsp_adapter_check_handoff(0, 0U, &rep) == NVX_GSP_HANDOFF_INVALID_ARGUMENT);
    r.length = 64U;
    assert(nvx_gsp_adapter_check_handoff(&r, 0U, &rep) == NVX_GSP_HANDOFF_INVALID_ARGUMENT);
    return 0;
}
