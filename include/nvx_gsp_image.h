/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_IMAGE_H
#define VANTA_NVX_GSP_IMAGE_H
#include <stdint.h>
#include "nvx_gsp_bundle.h"
#define NVX_GSP_IMAGE_MAX_SIGNATURES 15U
#define NVX_GSP_IMAGE_MAX_APPS 1U
typedef enum nvx_gsp_image_status {
    NVX_GSP_IMAGE_OK = 0, NVX_GSP_IMAGE_INVALID_ARGUMENT,
    NVX_GSP_IMAGE_TRUNCATED, NVX_GSP_IMAGE_BAD_MAGIC, NVX_GSP_IMAGE_RANGE_INVALID,
    NVX_GSP_IMAGE_HEADER_INVALID, NVX_GSP_IMAGE_SIGNATURE_INVALID,
    NVX_GSP_IMAGE_METADATA_INVALID, NVX_GSP_IMAGE_APP_INVALID,
    NVX_GSP_IMAGE_UNSUPPORTED_LAYOUT,
    NVX_GSP_IMAGE_FUSE_VERSION_UNSUPPORTED,
    NVX_GSP_IMAGE_STAGING_TOO_SMALL,
    NVX_GSP_IMAGE_UNSUPPORTED_VERSION
} nvx_gsp_image_status_t;
typedef struct nvx_gsp_image_info {
    uint32_t binary_magic, binary_version, binary_size;
    uint32_t data_offset, data_size;
    uint32_t fuse_version; /* Descriptor baseline; not the live hardware fuse value. */
    uint32_t engine_id, ucode_id;
    uint32_t signature_count, signature_size, signature_offset;
    uint32_t patch_location, patch_signature;
    uint32_t load_header_offset, load_header_size;
    uint32_t os_code_offset, os_code_size, os_data_offset, os_data_size;
    uint32_t app_count, app0_offset, app0_size, app0_data_offset, app0_data_size;
} nvx_gsp_image_info_t;
/* Read-only structural validation. This neither authenticates nor boots firmware. */
nvx_gsp_image_status_t nvx_gsp_image_parse(
    const nvx_gsp_component_t *component, nvx_gsp_image_info_t *out_info);
/*
 * Copy the structurally validated payload and patch its signature into private
 * staging. fuse_version is the engine-specific hardware fuse value: zero
 * selects the final signature; nonzero values index from the firmware FUSE
 * baseline. The provider must separately establish firmware authenticity.
 */
nvx_gsp_image_status_t nvx_gsp_image_prepare(
    const nvx_gsp_component_t *component, uint32_t hardware_fuse_version,
    uint8_t *staging, size_t staging_size);
#endif
