/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_fwsec_command.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

int main(void)
{
    uint8_t out[NVX_GSP_FWSEC_FRTS_COMMAND_SIZE];
    uint8_t before[sizeof(out)];
    size_t size = 999U;

    memset(out, 0xA5, sizeof(out));
    assert(nvx_gsp_fwsec_build_command(NVX_GSP_FWSEC_CMD_SB, 0U,
        out, sizeof(out), &size) == NVX_GSP_FWSEC_COMMAND_OK);
    assert(size == NVX_GSP_FWSEC_READ_VBIOS_DESC_SIZE);
    assert(get32(out) == 1U);
    assert(get32(out + 4U) == 24U);
    assert(get32(out + 8U) == 0U && get32(out + 12U) == 0U);
    assert(get32(out + 16U) == 0U && get32(out + 20U) == 2U);

    memset(out, 0xA5, sizeof(out));
    assert(nvx_gsp_fwsec_build_command(NVX_GSP_FWSEC_CMD_FRTS,
        0x200000U, out, sizeof(out), &size) == NVX_GSP_FWSEC_COMMAND_OK);
    assert(size == NVX_GSP_FWSEC_FRTS_COMMAND_SIZE);
    assert(get32(out) == 1U && get32(out + 4U) == 24U);
    assert(get32(out + 24U) == 1U && get32(out + 28U) == 20U);
    assert(get32(out + 32U) == 0x200U);
    assert(get32(out + 36U) == 0x100U);
    assert(get32(out + 40U) == NVX_GSP_FWSEC_FRTS_MEDIA_FB);

    memset(out, 0xA5, sizeof(out));
    memcpy(before, out, sizeof(out));
    assert(nvx_gsp_fwsec_build_command(NVX_GSP_FWSEC_CMD_FRTS,
        0x200001U, out, sizeof(out), &size) ==
        NVX_GSP_FWSEC_COMMAND_BAD_ALIGNMENT);
    assert(size == 0U && memcmp(out, before, sizeof(out)) == 0);
    assert(nvx_gsp_fwsec_build_command(NVX_GSP_FWSEC_CMD_FRTS,
        0U, out, sizeof(out), &size) ==
        NVX_GSP_FWSEC_COMMAND_INVALID_ARGUMENT);
    assert(nvx_gsp_fwsec_build_command(0xDEADU, 0U, out,
        sizeof(out), &size) == NVX_GSP_FWSEC_COMMAND_UNSUPPORTED_ID);

    memset(out, 0xA5, sizeof(out));
    memcpy(before, out, sizeof(out));
    assert(nvx_gsp_fwsec_build_command(NVX_GSP_FWSEC_CMD_FRTS,
        0x1000U, out, 16U, &size) ==
        NVX_GSP_FWSEC_COMMAND_BUFFER_TOO_SMALL);
    assert(size == 0U && memcmp(out, before, sizeof(out)) == 0);

    assert(nvx_gsp_fwsec_build_command(NVX_GSP_FWSEC_CMD_SB, 0U,
        out, sizeof(out), NULL) == NVX_GSP_FWSEC_COMMAND_INVALID_ARGUMENT);
    assert(nvx_gsp_fwsec_build_command(NVX_GSP_FWSEC_CMD_SB, 0U,
        out, sizeof(out), (size_t *)(void *)out) ==
        NVX_GSP_FWSEC_COMMAND_OVERLAPPING_OUTPUT);

    puts("NVX FWSEC command builder tests passed");
    return 0;
}
