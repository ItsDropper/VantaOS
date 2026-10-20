/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_BUNDLE_H
#define VANTA_NVX_GSP_BUNDLE_H
#include <stdint.h>
#include "nvx_firmware.h"
#define NVX_GSP_COMPONENT_COUNT 4U
#define NVX_GSP_VERSION_MAX 31U
#define NVX_GSP_CHIP_MAX 15U
#define NVX_GSP_BUNDLE_MAX_BYTES (128U * 1024U * 1024U)
typedef enum nvx_gsp_component_kind {
    NVX_GSP_BOOTER_LOAD = 0, NVX_GSP_BOOTER_UNLOAD,
    NVX_GSP_BOOTLOADER, NVX_GSP_RUNTIME
} nvx_gsp_component_kind_t;
typedef struct nvx_gsp_component {
    nvx_firmware_blob_t blob;
    nvx_gsp_component_kind_t kind;
    uint8_t acquired;
} nvx_gsp_component_t;
typedef enum nvx_gsp_bundle_status {
    NVX_GSP_BUNDLE_OK = 0, NVX_GSP_BUNDLE_INVALID_ARGUMENT,
    NVX_GSP_BUNDLE_UNSUPPORTED_FAMILY, NVX_GSP_BUNDLE_PROVIDER_INCOMPLETE,
    NVX_GSP_BUNDLE_COMPONENT_NOT_FOUND, NVX_GSP_BUNDLE_INVALID_COMPONENT,
    NVX_GSP_BUNDLE_COMPONENT_TOO_LARGE, NVX_GSP_BUNDLE_TOTAL_TOO_LARGE,
    NVX_GSP_BUNDLE_VERIFICATION_FAILED, NVX_GSP_BUNDLE_ALREADY_ACQUIRED
} nvx_gsp_bundle_status_t;
typedef struct nvx_gsp_bundle {
    nvx_firmware_provider_t provider;
    nvx_gsp_component_t components[NVX_GSP_COMPONENT_COUNT];
    char chip[NVX_GSP_CHIP_MAX + 1U];
    char version[NVX_GSP_VERSION_MAX + 1U];
    uint64_t total_bytes;
    nvx_generation_family_t family;
    uint8_t acquired;
} nvx_gsp_bundle_t;
/* Caller supplies exact chip directory and version. Zero-init before first use.
 * Acquires/verifies four version-matched files; does not parse or boot firmware. */
nvx_gsp_bundle_status_t nvx_gsp_bundle_acquire(
    const nvx_firmware_provider_t *, nvx_generation_family_t,
    const char *chip, const char *version, nvx_gsp_bundle_t *out_bundle);
void nvx_gsp_bundle_release(nvx_gsp_bundle_t *bundle);
#endif
