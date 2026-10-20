/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_FWSEC_INTERFACE_H
#define VANTA_NVX_GSP_FWSEC_INTERFACE_H

#include <stddef.h>
#include <stdint.h>

typedef enum nvx_gsp_fwsec_interface_status {
    NVX_GSP_FWSEC_INTERFACE_OK = 0,
    NVX_GSP_FWSEC_INTERFACE_INVALID_ARGUMENT,
    NVX_GSP_FWSEC_INTERFACE_BAD_HEADER,
    NVX_GSP_FWSEC_INTERFACE_RANGE_ERROR,
    NVX_GSP_FWSEC_INTERFACE_NOT_FOUND,
    NVX_GSP_FWSEC_INTERFACE_AMBIGUOUS,
    NVX_GSP_FWSEC_INTERFACE_BAD_MAPPER,
    NVX_GSP_FWSEC_INTERFACE_COMMAND_TOO_LARGE,
    NVX_GSP_FWSEC_INTERFACE_OVERLAPPING_OUTPUT
} nvx_gsp_fwsec_interface_status_t;

typedef struct nvx_gsp_fwsec_command_window {
    uint32_t mapper_offset;
    uint32_t mapper_size;
    uint32_t init_cmd_offset;
    uint32_t command_input_offset;
    uint32_t command_input_capacity;
    uint32_t command_id;
} nvx_gsp_fwsec_command_window_t;

/*
 * Parse the FWSEC application interface inside the firmware's DMEM image.
 *
 * This is a read-only planner: it never writes init_cmd or command bytes.
 * interface_offset is the descriptor's InterfaceOffset, relative to DMEM.
 * The parser supports interface header version 1 and DMEM mapper version 3.
 * All offsets returned are DMEM-relative. The caller must perform any
 * firmware-specific command patching only after validating the returned
 * window and preserving the DMA ownership/coherency contract.
 */
nvx_gsp_fwsec_interface_status_t nvx_gsp_fwsec_find_command_window(
    const void *dmem, size_t dmem_size, uint32_t interface_offset,
    uint32_t command_id, size_t command_size,
    nvx_gsp_fwsec_command_window_t *out_window);

#endif
