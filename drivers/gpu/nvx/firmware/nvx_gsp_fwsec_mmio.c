/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 *
 * Register locations and result fields are independently reconstructed from
 * NVIDIA's public GA102/TU102 register headers and FWSEC execution path.
 */
#include "nvx_gsp_fwsec_mmio.h"

#include <string.h>

static int overlaps(const void *a, size_t a_size,
                    const void *b, size_t b_size)
{
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;

    if (a_size == 0U || b_size == 0U)
        return 0;
    if (x > UINTPTR_MAX - a_size || y > UINTPTR_MAX - b_size)
        return 1;
    return x < y + b_size && y < x + a_size;
}

static nvx_gsp_fwsec_mmio_status_t read_register(
    const nvx_mmio_region_t *bar0, uint32_t offset, uint32_t *value)
{
    return nvx_mmio_read32(bar0, offset, value) == NVX_MMIO_OK
        ? NVX_GSP_FWSEC_MMIO_OK : NVX_GSP_FWSEC_MMIO_READ_FAILED;
}

nvx_gsp_fwsec_mmio_status_t nvx_gsp_fwsec_read_result_snapshot(
    const nvx_mmio_region_t *bar0, uint64_t requested_frts_offset,
    nvx_gsp_fwsec_result_snapshot_t *out_snapshot)
{
    nvx_gsp_fwsec_result_snapshot_t snapshot = {0};
    nvx_gsp_fwsec_wpr2_fields_t wpr2 = {0};
    uint32_t scratch_frts, scratch_sb, wpr2_lo, wpr2_hi;
    uint32_t gfw_plm, gfw_progress;

    if (!bar0 || !bar0->base || !out_snapshot)
        return NVX_GSP_FWSEC_MMIO_INVALID_ARGUMENT;
    if (overlaps(out_snapshot, sizeof(*out_snapshot), bar0, sizeof(*bar0)) ||
        overlaps(out_snapshot, sizeof(*out_snapshot),
                 (const void *)bar0->base, bar0->length))
        return NVX_GSP_FWSEC_MMIO_OUTPUT_OVERLAP;
    memset(out_snapshot, 0, sizeof(*out_snapshot));

    if ((requested_frts_offset & 0xFFFU) != 0U ||
        (requested_frts_offset >> 12U) > 0x0FFFFFFFU)
        return NVX_GSP_FWSEC_MMIO_BAD_OFFSET;

    if (read_register(bar0, NVX_GSP_FWSEC_VBIOS_SCRATCH(
                          NVX_GSP_FWSEC_SCRATCH_FRTS_INDEX), &scratch_frts) != NVX_GSP_FWSEC_MMIO_OK ||
        read_register(bar0, NVX_GSP_FWSEC_VBIOS_SCRATCH(
                          NVX_GSP_FWSEC_SCRATCH_SB_INDEX), &scratch_sb) != NVX_GSP_FWSEC_MMIO_OK ||
        read_register(bar0, NVX_GSP_FWSEC_GFW_PLM, &gfw_plm) != NVX_GSP_FWSEC_MMIO_OK ||
        read_register(bar0, NVX_GSP_FWSEC_GFW_PROGRESS, &gfw_progress) != NVX_GSP_FWSEC_MMIO_OK ||
        read_register(bar0, NVX_GSP_FWSEC_WPR2_ADDR_LO, &wpr2_lo) != NVX_GSP_FWSEC_MMIO_OK ||
        read_register(bar0, NVX_GSP_FWSEC_WPR2_ADDR_HI, &wpr2_hi) != NVX_GSP_FWSEC_MMIO_OK)
        return NVX_GSP_FWSEC_MMIO_READ_FAILED;

    if (nvx_gsp_fwsec_decode_wpr2_registers(
            wpr2_lo, wpr2_hi, requested_frts_offset, &wpr2) !=
        NVX_GSP_FWSEC_RESULT_OK)
        return NVX_GSP_FWSEC_MMIO_BAD_OFFSET;

    snapshot.frts_error_code = (scratch_frts >> 16U) & 0xFFFFU;
    snapshot.sb_error_code = scratch_sb & 0xFFFFU;
    snapshot.wpr2_addr_hi = wpr2.address_hi_units;
    snapshot.wpr2_addr_lo_units = wpr2.address_lo_units;
    snapshot.expected_wpr2_addr_lo_units = wpr2.expected_address_lo_units;
    snapshot.gfw_plm_lowered = (uint8_t)(
        (gfw_plm & NVX_GSP_FWSEC_PLM_READ_LEVEL0_ENABLE) != 0U);
    snapshot.gfw_progress_completed = (uint8_t)(
        (gfw_progress & 0xFFU) == NVX_GSP_FWSEC_GFW_PROGRESS_COMPLETED);

    *out_snapshot = snapshot;
    return NVX_GSP_FWSEC_MMIO_OK;
}
