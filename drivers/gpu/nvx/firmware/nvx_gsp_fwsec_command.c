/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 *
 * Independently serializes the public FWSEC SB/FRTS input records documented
 * in NVIDIA's MIT-licensed kernel_gsp_frts_tu102.c. No reference implementation
 * code is copied here.
 */
#include "nvx_gsp_fwsec_command.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

static int overlaps(const void *a, size_t as, const void *b, size_t bs)
{
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;

    if (as == 0U || bs == 0U)
        return 0;
    if (x > UINTPTR_MAX - as || y > UINTPTR_MAX - bs)
        return 1;
    return x < y + bs && y < x + as;
}

static void put_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
}

static void put_le64(uint8_t *p, uint64_t value)
{
    put_le32(p, (uint32_t)value);
    put_le32(p + 4U, (uint32_t)(value >> 32U));
}

nvx_gsp_fwsec_command_status_t nvx_gsp_fwsec_build_command(
    uint32_t command_id, uint64_t frts_fb_offset,
    void *out_command, size_t out_capacity, size_t *out_size)
{
    uint8_t local[NVX_GSP_FWSEC_FRTS_COMMAND_SIZE] = {0};
    size_t required;

    if (!out_size)
        return NVX_GSP_FWSEC_COMMAND_INVALID_ARGUMENT;
    if (out_command && overlaps(out_command, out_capacity,
                                out_size, sizeof(*out_size))) {
        return NVX_GSP_FWSEC_COMMAND_OVERLAPPING_OUTPUT;
    }
    *out_size = 0U;
    if (!out_command || out_capacity == 0U)
        return NVX_GSP_FWSEC_COMMAND_INVALID_ARGUMENT;

    if (command_id == NVX_GSP_FWSEC_CMD_SB) {
        required = NVX_GSP_FWSEC_READ_VBIOS_DESC_SIZE;
    } else if (command_id == NVX_GSP_FWSEC_CMD_FRTS) {
        if (frts_fb_offset == 0U)
            return NVX_GSP_FWSEC_COMMAND_INVALID_ARGUMENT;
        if ((frts_fb_offset & 0xFFFU) != 0U)
            return NVX_GSP_FWSEC_COMMAND_BAD_ALIGNMENT;
        if ((frts_fb_offset >> 12U) > UINT32_MAX)
            return NVX_GSP_FWSEC_COMMAND_RANGE_ERROR;
        required = NVX_GSP_FWSEC_FRTS_COMMAND_SIZE;
    } else {
        return NVX_GSP_FWSEC_COMMAND_UNSUPPORTED_ID;
    }

    if (out_capacity < required)
        return NVX_GSP_FWSEC_COMMAND_BUFFER_TOO_SMALL;

    /* READ_VBIOS_DESC v1: version, structure size, image offset/size, flags. */
    put_le32(local, 1U);
    put_le32(local + 4U, NVX_GSP_FWSEC_READ_VBIOS_DESC_SIZE);
    put_le64(local + 8U, 0U);
    put_le32(local + 16U, 0U);
    put_le32(local + 20U, NVX_GSP_FWSEC_READ_VBIOS_FLAGS);

    if (command_id == NVX_GSP_FWSEC_CMD_FRTS) {
        uint8_t *frts = local + NVX_GSP_FWSEC_READ_VBIOS_DESC_SIZE;
        put_le32(frts, 1U);
        put_le32(frts + 4U, NVX_GSP_FWSEC_FRTS_DESC_SIZE);
        put_le32(frts + 8U, (uint32_t)(frts_fb_offset >> 12U));
        put_le32(frts + 12U, NVX_GSP_FWSEC_FRTS_SIZE_1MB_IN_4K);
        put_le32(frts + 16U, NVX_GSP_FWSEC_FRTS_MEDIA_FB);
        required = NVX_GSP_FWSEC_FRTS_COMMAND_SIZE;
    }

    memcpy(out_command, local, required);
    *out_size = required;
    return NVX_GSP_FWSEC_COMMAND_OK;
}
