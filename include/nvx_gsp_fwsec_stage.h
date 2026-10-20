/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_FWSEC_STAGE_H
#define VANTA_NVX_GSP_FWSEC_STAGE_H

#include "nvx_gsp_fwsec_interface.h"
#include "nvx_gsp_fwsec_command.h"

typedef enum nvx_gsp_fwsec_stage_status {
    NVX_GSP_FWSEC_STAGE_OK = 0,
    NVX_GSP_FWSEC_STAGE_INVALID_ARGUMENT,
    NVX_GSP_FWSEC_STAGE_OUTPUT_OVERLAP,
    NVX_GSP_FWSEC_STAGE_COMMAND_BUILD_FAILED,
    NVX_GSP_FWSEC_STAGE_INTERFACE_FAILED
} nvx_gsp_fwsec_stage_status_t;

/*
 * Validate the FWSEC V1 interface, build the SB/FRTS input record, copy it to
 * the validated DMEM command-input range, and publish init_cmd last. All
 * validation completes before DMEM is changed. This stages command bytes only:
 * it does not transfer DMA ownership, start SEC2, or claim command success.
 */
nvx_gsp_fwsec_stage_status_t nvx_gsp_fwsec_stage_command(
    void *dmem, size_t dmem_size, uint32_t interface_offset,
    uint32_t command_id, uint64_t frts_fb_offset,
    nvx_gsp_fwsec_command_window_t *out_window);

#endif
