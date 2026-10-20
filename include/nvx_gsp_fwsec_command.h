/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_FWSEC_COMMAND_H
#define VANTA_NVX_GSP_FWSEC_COMMAND_H

#include <stddef.h>
#include <stdint.h>

#define NVX_GSP_FWSEC_CMD_FRTS 0x15U
#define NVX_GSP_FWSEC_CMD_SB   0x19U
#define NVX_GSP_FWSEC_READ_VBIOS_DESC_SIZE 24U
#define NVX_GSP_FWSEC_FRTS_DESC_SIZE 20U
#define NVX_GSP_FWSEC_FRTS_COMMAND_SIZE 44U
#define NVX_GSP_FWSEC_FRTS_SIZE_1MB_IN_4K 0x100U
#define NVX_GSP_FWSEC_FRTS_MEDIA_FB 2U
#define NVX_GSP_FWSEC_READ_VBIOS_FLAGS 2U

typedef enum nvx_gsp_fwsec_command_status {
    NVX_GSP_FWSEC_COMMAND_OK = 0,
    NVX_GSP_FWSEC_COMMAND_INVALID_ARGUMENT,
    NVX_GSP_FWSEC_COMMAND_UNSUPPORTED_ID,
    NVX_GSP_FWSEC_COMMAND_BAD_ALIGNMENT,
    NVX_GSP_FWSEC_COMMAND_RANGE_ERROR,
    NVX_GSP_FWSEC_COMMAND_BUFFER_TOO_SMALL,
    NVX_GSP_FWSEC_COMMAND_OVERLAPPING_OUTPUT
} nvx_gsp_fwsec_command_status_t;

/*
 * Build the version-1 FWSEC SB or FRTS input record as explicit little-endian
 * bytes. This pure builder does not patch DMEM, copy signatures, transfer DMA
 * ownership, or start SEC2. frts_fb_offset is a framebuffer byte offset and
 * must be nonzero and 4 KiB aligned for FRTS; it is ignored for SB.
 *
 * On failure, out_size is set to zero where safe and out_command is untouched.
 */
nvx_gsp_fwsec_command_status_t nvx_gsp_fwsec_build_command(
    uint32_t command_id, uint64_t frts_fb_offset,
    void *out_command, size_t out_capacity, size_t *out_size);

#endif
