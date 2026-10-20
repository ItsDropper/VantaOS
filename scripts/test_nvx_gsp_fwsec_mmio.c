/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_fwsec_mmio.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint32_t bar_words[(NVX_GSP_FWSEC_MMIO_REQUIRED_BAR_BYTES + 3U) / 4U];

static void put32(uint8_t *bar, uint32_t offset, uint32_t value)
{
    bar[offset] = (uint8_t)value;
    bar[offset + 1U] = (uint8_t)(value >> 8U);
    bar[offset + 2U] = (uint8_t)(value >> 16U);
    bar[offset + 3U] = (uint8_t)(value >> 24U);
}

int main(void)
{
    const uint64_t frts_offset = 0x30000000ULL;
    uint8_t *bar = (uint8_t *)(void *)bar_words;
    nvx_mmio_region_t region = {bar, sizeof(bar_words)};
    nvx_gsp_fwsec_result_snapshot_t snapshot;

    memset(bar_words, 0, sizeof(bar_words));
    put32(bar, NVX_GSP_FWSEC_VBIOS_SCRATCH(NVX_GSP_FWSEC_SCRATCH_FRTS_INDEX), 0U);
    put32(bar, NVX_GSP_FWSEC_VBIOS_SCRATCH(NVX_GSP_FWSEC_SCRATCH_SB_INDEX), 0U);
    put32(bar, NVX_GSP_FWSEC_GFW_PLM, NVX_GSP_FWSEC_PLM_READ_LEVEL0_ENABLE);
    put32(bar, NVX_GSP_FWSEC_GFW_PROGRESS, NVX_GSP_FWSEC_GFW_PROGRESS_COMPLETED);
    put32(bar, NVX_GSP_FWSEC_WPR2_ADDR_LO, (uint32_t)((frts_offset >> 12U) << 4U));
    put32(bar, NVX_GSP_FWSEC_WPR2_ADDR_HI, 0x1000U);

    assert(nvx_gsp_fwsec_read_result_snapshot(
        &region, frts_offset, &snapshot) == NVX_GSP_FWSEC_MMIO_OK);
    assert(snapshot.frts_error_code == 0U && snapshot.sb_error_code == 0U);
    assert(snapshot.wpr2_addr_hi != 0U);
    assert(snapshot.wpr2_addr_lo_units == (uint32_t)(frts_offset >> 12U));
    assert(snapshot.expected_wpr2_addr_lo_units == (uint32_t)(frts_offset >> 12U));
    assert(snapshot.gfw_plm_lowered && snapshot.gfw_progress_completed);
    assert(nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_FRTS, &snapshot) ==
           NVX_GSP_FWSEC_RESULT_OK);
    assert(nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_SB, &snapshot) ==
           NVX_GSP_FWSEC_RESULT_OK);

    put32(bar, NVX_GSP_FWSEC_VBIOS_SCRATCH(NVX_GSP_FWSEC_SCRATCH_FRTS_INDEX), 0x00010000U);
    assert(nvx_gsp_fwsec_read_result_snapshot(
        &region, frts_offset, &snapshot) == NVX_GSP_FWSEC_MMIO_OK);
    assert(nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_FRTS, &snapshot) ==
           NVX_GSP_FWSEC_RESULT_FRTS_ERROR);

    put32(bar, NVX_GSP_FWSEC_VBIOS_SCRATCH(NVX_GSP_FWSEC_SCRATCH_FRTS_INDEX), 0U);
    put32(bar, NVX_GSP_FWSEC_VBIOS_SCRATCH(NVX_GSP_FWSEC_SCRATCH_SB_INDEX), 1U);
    assert(nvx_gsp_fwsec_read_result_snapshot(
        &region, frts_offset, &snapshot) == NVX_GSP_FWSEC_MMIO_OK);
    assert(nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_SB, &snapshot) ==
           NVX_GSP_FWSEC_RESULT_SB_ERROR);

    memset(&snapshot, 0xA5, sizeof(snapshot));
    assert(nvx_gsp_fwsec_read_result_snapshot(
        &region, frts_offset + 1U, &snapshot) == NVX_GSP_FWSEC_MMIO_BAD_OFFSET);
    {
        const nvx_gsp_fwsec_result_snapshot_t zero = {0};
        assert(memcmp(&snapshot, &zero, sizeof(snapshot)) == 0);
    }

    memset(&snapshot, 0xA5, sizeof(snapshot));
    nvx_mmio_region_t short_region = {bar, NVX_GSP_FWSEC_WPR2_ADDR_HI};
    assert(nvx_gsp_fwsec_read_result_snapshot(
        &short_region, frts_offset, &snapshot) == NVX_GSP_FWSEC_MMIO_READ_FAILED);
    {
        const nvx_gsp_fwsec_result_snapshot_t zero = {0};
        assert(memcmp(&snapshot, &zero, sizeof(snapshot)) == 0);
    }

    assert(nvx_gsp_fwsec_read_result_snapshot(
        &region, frts_offset,
        (nvx_gsp_fwsec_result_snapshot_t *)(void *)(bar + 0x100U)) ==
        NVX_GSP_FWSEC_MMIO_OUTPUT_OVERLAP);
    return 0;
}
