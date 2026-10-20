/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_fwsec_result.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    nvx_gsp_fwsec_result_snapshot_t s;
    memset(&s, 0, sizeof(s));
    nvx_gsp_fwsec_wpr2_fields_t fields;
    memset(&fields, 0xA5, sizeof(fields));
    assert(nvx_gsp_fwsec_decode_wpr2_registers(
        0x0012345FU, 0x0000002FU, UINT64_C(0x12345000), &fields) ==
        NVX_GSP_FWSEC_RESULT_OK);
    assert(fields.address_lo_units == 0x12345U);
    assert(fields.address_hi_units == 2U);
    assert(fields.expected_address_lo_units == 0x12345U);
    {
        nvx_gsp_fwsec_wpr2_fields_t before = fields;
        assert(nvx_gsp_fwsec_decode_wpr2_registers(
            0U, 0U, UINT64_C(0x12345001), &fields) ==
            NVX_GSP_FWSEC_RESULT_INVALID_ARGUMENT);
        assert(memcmp(&fields, &before, sizeof(fields)) == 0);
        assert(nvx_gsp_fwsec_decode_wpr2_registers(
            0U, 0U, (UINT64_C(0x10000000) << 12U), &fields) ==
            NVX_GSP_FWSEC_RESULT_INVALID_ARGUMENT);
        assert(nvx_gsp_fwsec_decode_wpr2_registers(
            0U, 0U, 0U, NULL) == NVX_GSP_FWSEC_RESULT_INVALID_ARGUMENT);
    }

    s.wpr2_addr_hi = 1U;
    s.wpr2_addr_lo_units = 0x200U;
    s.expected_wpr2_addr_lo_units = 0x200U;
    assert(nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_FRTS, &s) ==
           NVX_GSP_FWSEC_RESULT_OK);
    s.frts_error_code = 7U;
    assert(nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_FRTS, &s) ==
           NVX_GSP_FWSEC_RESULT_FRTS_ERROR);
    s.frts_error_code = 0U;
    s.wpr2_addr_hi = 0U;
    assert(nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_FRTS, &s) ==
           NVX_GSP_FWSEC_RESULT_WPR2_MISSING);
    s.wpr2_addr_hi = 1U;
    s.wpr2_addr_lo_units++;
    assert(nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_FRTS, &s) ==
           NVX_GSP_FWSEC_RESULT_WPR2_OFFSET_MISMATCH);

    memset(&s, 0, sizeof(s));
    s.gfw_plm_lowered = 1U;
    s.gfw_progress_completed = 1U;
    assert(nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_SB, &s) ==
           NVX_GSP_FWSEC_RESULT_OK);
    s.gfw_plm_lowered = 0U;
    assert(nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_SB, &s) ==
           NVX_GSP_FWSEC_RESULT_GFW_PLM_NOT_LOWERED);
    s.gfw_plm_lowered = 1U;
    s.gfw_progress_completed = 0U;
    assert(nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_SB, &s) ==
           NVX_GSP_FWSEC_RESULT_GFW_PROGRESS_INCOMPLETE);
    s.gfw_progress_completed = 1U;
    s.sb_error_code = 3U;
    assert(nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_SB, &s) ==
           NVX_GSP_FWSEC_RESULT_SB_ERROR);
    assert(nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_SB, NULL) ==
           NVX_GSP_FWSEC_RESULT_INVALID_ARGUMENT);
    assert(nvx_gsp_fwsec_validate_result(0xDEADU, &s) ==
           NVX_GSP_FWSEC_RESULT_UNSUPPORTED_COMMAND);

    puts("NVX FWSEC result validation tests passed");
    return 0;
}
