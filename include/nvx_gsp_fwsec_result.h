/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_FWSEC_RESULT_H
#define VANTA_NVX_GSP_FWSEC_RESULT_H

#include <stdint.h>
#include "nvx_gsp_fwsec_command.h"

typedef enum nvx_gsp_fwsec_result_status {
    NVX_GSP_FWSEC_RESULT_OK = 0,
    NVX_GSP_FWSEC_RESULT_INVALID_ARGUMENT,
    NVX_GSP_FWSEC_RESULT_UNSUPPORTED_COMMAND,
    NVX_GSP_FWSEC_RESULT_FRTS_ERROR,
    NVX_GSP_FWSEC_RESULT_WPR2_MISSING,
    NVX_GSP_FWSEC_RESULT_WPR2_OFFSET_MISMATCH,
    NVX_GSP_FWSEC_RESULT_GFW_PLM_NOT_LOWERED,
    NVX_GSP_FWSEC_RESULT_GFW_PROGRESS_INCOMPLETE,
    NVX_GSP_FWSEC_RESULT_SB_ERROR
} nvx_gsp_fwsec_result_status_t;

/*
 * Already-decoded result snapshot from the platform register reader.
 * wpr2_addr_lo_units and expected_wpr2_addr_lo_units must use the same units:
 * the caller extracts the WPR2 LO field and applies the hardware-defined
 * NV_PFB_PRI_MMU_WPR2_ADDR_LO_ALIGNMENT shift. This API does not access MMIO.
 */
typedef struct nvx_gsp_fwsec_result_snapshot {
    uint32_t frts_error_code;
    uint32_t sb_error_code;
    uint32_t wpr2_addr_hi;
    uint32_t wpr2_addr_lo_units;
    uint32_t expected_wpr2_addr_lo_units;
    uint8_t gfw_plm_lowered;
    uint8_t gfw_progress_completed;
} nvx_gsp_fwsec_result_snapshot_t;

/* Validate post-execution evidence in the same fail-fast order as the public
 * GA10x/TU102 result path. This does not execute a command or imply hardware
 * success unless the supplied snapshot came from actual hardware. */
nvx_gsp_fwsec_result_status_t nvx_gsp_fwsec_validate_result(
    uint32_t command_id, const nvx_gsp_fwsec_result_snapshot_t *snapshot);

#endif
