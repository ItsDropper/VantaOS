/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 *
 * Independently stages the public GA10x FWSEC SB/FRTS command contract.
 * This does not execute firmware or copy NVIDIA implementation code.
 */
#include "nvx_gsp_fwsec_stage.h"

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

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
}

nvx_gsp_fwsec_stage_status_t nvx_gsp_fwsec_stage_command(
    void *dmem, size_t dmem_size, uint32_t interface_offset,
    uint32_t command_id, uint64_t frts_fb_offset,
    nvx_gsp_fwsec_command_window_t *out_window)
{
    uint8_t command[NVX_GSP_FWSEC_FRTS_COMMAND_SIZE] = {0};
    size_t command_size = 0U;
    nvx_gsp_fwsec_command_window_t window = {0};
    nvx_gsp_fwsec_command_status_t command_status;
    nvx_gsp_fwsec_interface_status_t interface_status;
    uint8_t *bytes = (uint8_t *)dmem;

    if (!dmem || !out_window || dmem_size == 0U ||
        dmem_size > UINT32_MAX)
        return NVX_GSP_FWSEC_STAGE_INVALID_ARGUMENT;
    if (overlaps(dmem, dmem_size, out_window, sizeof(*out_window)))
        return NVX_GSP_FWSEC_STAGE_OUTPUT_OVERLAP;

    memset(out_window, 0, sizeof(*out_window));
    command_status = nvx_gsp_fwsec_build_command(
        command_id, frts_fb_offset, command, sizeof(command), &command_size);
    if (command_status != NVX_GSP_FWSEC_COMMAND_OK)
        return NVX_GSP_FWSEC_STAGE_COMMAND_BUILD_FAILED;

    interface_status = nvx_gsp_fwsec_find_command_window(
        dmem, dmem_size, interface_offset, command_id, command_size, &window);
    if (interface_status != NVX_GSP_FWSEC_INTERFACE_OK)
        return NVX_GSP_FWSEC_STAGE_INTERFACE_FAILED;

    /*
     * No operation below this point can fail. Write command input first and
     * init_cmd last so firmware cannot observe a published command before its
     * complete input record is present in DMEM.
     */
    memcpy(bytes + window.command_input_offset, command, command_size);
    write_le32(bytes + window.init_cmd_offset, command_id);
    *out_window = window;
    return NVX_GSP_FWSEC_STAGE_OK;
}
