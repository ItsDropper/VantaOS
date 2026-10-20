/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_BOOTLOADER_H
#define VANTA_NVX_GSP_BOOTLOADER_H

#include <stddef.h>
#include <stdint.h>
#include "nvx_gsp_bundle.h"

#define NVX_GSP_BOOTLOADER_DESC_GA10X_SIZE 84U
#define NVX_GSP_BOOTLOADER_ALIGNMENT 256U

typedef enum nvx_gsp_bootloader_status {
    NVX_GSP_BOOTLOADER_OK = 0,
    NVX_GSP_BOOTLOADER_INVALID_ARGUMENT,
    NVX_GSP_BOOTLOADER_BAD_MAGIC,
    NVX_GSP_BOOTLOADER_UNSUPPORTED_VERSION,
    NVX_GSP_BOOTLOADER_BAD_LAYOUT,
    NVX_GSP_BOOTLOADER_RANGE_INVALID,
    NVX_GSP_BOOTLOADER_BUFFER_TOO_SMALL
} nvx_gsp_bootloader_status_t;

typedef struct nvx_gsp_bootloader_info {
    uint32_t binary_size;
    uint32_t descriptor_offset;
    uint32_t descriptor_size;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint32_t descriptor_version;
    uint32_t manifest_offset, manifest_size;
    uint32_t monitor_data_offset, monitor_data_size;
    uint32_t monitor_code_offset, monitor_code_size;
} nvx_gsp_bootloader_info_t;

/* Validate the GA10x external bootloader.bin container without modifying it. */
nvx_gsp_bootloader_status_t nvx_gsp_bootloader_parse(
    const nvx_gsp_component_t *component, nvx_gsp_bootloader_info_t *out_info);

/* Copy only the bootloader firmware payload into private, non-aliasing staging. */
nvx_gsp_bootloader_status_t nvx_gsp_bootloader_prepare(
    const nvx_gsp_component_t *component, uint8_t *staging,
    size_t staging_capacity, nvx_gsp_bootloader_info_t *out_info);

#endif
