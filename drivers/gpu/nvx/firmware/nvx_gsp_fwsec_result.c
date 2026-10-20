/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 *
 * Independent validation of result fields documented in NVIDIA's public
 * kernel_gsp_frts_tu102.c. This layer receives decoded fields and does no MMIO.
 */
#include "nvx_gsp_fwsec_result.h"

#define NVX_GSP_WPR2_VALUE_MASK 0x0FFFFFFFU
#define NVX_GSP_WPR2_ALIGNMENT_SHIFT 12U

nvx_gsp_fwsec_result_status_t nvx_gsp_fwsec_decode_wpr2_registers(
    uint32_t raw_address_lo, uint32_t raw_address_hi,
    uint64_t requested_frts_offset,
    nvx_gsp_fwsec_wpr2_fields_t *out_fields)
{
    nvx_gsp_fwsec_wpr2_fields_t fields;

    if (!out_fields ||
        (requested_frts_offset & ((UINT64_C(1) <<
                                   NVX_GSP_WPR2_ALIGNMENT_SHIFT) - 1U)) != 0U ||
        (requested_frts_offset >> NVX_GSP_WPR2_ALIGNMENT_SHIFT) >
            NVX_GSP_WPR2_VALUE_MASK)
        return NVX_GSP_FWSEC_RESULT_INVALID_ARGUMENT;

    fields.address_lo_units =
        (raw_address_lo >> 4U) & NVX_GSP_WPR2_VALUE_MASK;
    fields.address_hi_units =
        (raw_address_hi >> 4U) & NVX_GSP_WPR2_VALUE_MASK;
    fields.expected_address_lo_units =
        (uint32_t)(requested_frts_offset >> NVX_GSP_WPR2_ALIGNMENT_SHIFT);
    *out_fields = fields;
    return NVX_GSP_FWSEC_RESULT_OK;
}


nvx_gsp_fwsec_result_status_t nvx_gsp_fwsec_validate_result(
    uint32_t command_id, const nvx_gsp_fwsec_result_snapshot_t *snapshot)
{
    if (!snapshot)
        return NVX_GSP_FWSEC_RESULT_INVALID_ARGUMENT;

    if (command_id == NVX_GSP_FWSEC_CMD_FRTS) {
        if (snapshot->frts_error_code != 0U)
            return NVX_GSP_FWSEC_RESULT_FRTS_ERROR;
        if (snapshot->wpr2_addr_hi == 0U)
            return NVX_GSP_FWSEC_RESULT_WPR2_MISSING;
        if (snapshot->wpr2_addr_lo_units !=
            snapshot->expected_wpr2_addr_lo_units)
            return NVX_GSP_FWSEC_RESULT_WPR2_OFFSET_MISMATCH;
        return NVX_GSP_FWSEC_RESULT_OK;
    }

    if (command_id == NVX_GSP_FWSEC_CMD_SB) {
        if (snapshot->gfw_plm_lowered == 0U)
            return NVX_GSP_FWSEC_RESULT_GFW_PLM_NOT_LOWERED;
        if (snapshot->gfw_progress_completed == 0U)
            return NVX_GSP_FWSEC_RESULT_GFW_PROGRESS_INCOMPLETE;
        if (snapshot->sb_error_code != 0U)
            return NVX_GSP_FWSEC_RESULT_SB_ERROR;
        return NVX_GSP_FWSEC_RESULT_OK;
    }

    return NVX_GSP_FWSEC_RESULT_UNSUPPORTED_COMMAND;
}
