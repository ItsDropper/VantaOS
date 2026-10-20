/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_PREFLIGHT_H
#define VANTA_NVX_GSP_PREFLIGHT_H
#include <stddef.h>
#include <stdint.h>
#include "nvx_gsp_elf.h"
#include "nvx_gsp_image.h"
#define NVX_GSP_GA10X_SIGNATURE_SECTION ".fwsignature_ga10x"
typedef enum nvx_gsp_preflight_status {
    NVX_GSP_PREFLIGHT_OK=0,NVX_GSP_PREFLIGHT_INVALID_ARGUMENT,
    NVX_GSP_PREFLIGHT_BUNDLE_NOT_READY,NVX_GSP_PREFLIGHT_COMPONENT_INVALID,
    NVX_GSP_PREFLIGHT_RUNTIME_INVALID,NVX_GSP_PREFLIGHT_CONTAINER_INVALID,
    NVX_GSP_PREFLIGHT_VERSION_MISMATCH,
    NVX_GSP_PREFLIGHT_UNSUPPORTED_CHIP,
    NVX_GSP_PREFLIGHT_STAGING_TOO_SMALL,
    NVX_GSP_PREFLIGHT_FUSE_VERSION_UNSUPPORTED,
    NVX_GSP_PREFLIGHT_STAGING_ALIAS
} nvx_gsp_preflight_status_t;
typedef struct nvx_gsp_preflight_report {
    nvx_gsp_elf_info_t runtime;
    nvx_gsp_image_info_t booter_load,booter_unload;
    nvx_gsp_elf_section_t firmware_image,firmware_version;
    char version[64];
    uint64_t runtime_bytes,ucode_archive_bytes,booter_load_bytes;
    uint64_t booter_unload_bytes,bootloader_bytes;
    uint8_t runtime_verified,ucode_archive_verified,boot_assets_present;
} nvx_gsp_preflight_report_t;
/* Structural preflight only: platform verification, ELF/container validation and
 * presence and structural validation of runtime/booter images and all five GA10x package assets. It does not boot GSP or authenticate
 * signatures cryptographically; the provider's verify callback owns that policy. */
nvx_gsp_preflight_status_t nvx_gsp_preflight_validate(
    const nvx_gsp_bundle_t *,nvx_gsp_preflight_report_t *out_report);
/* Validate the complete bundle, then stage both fuse-selected booter payloads. */
nvx_gsp_preflight_status_t nvx_gsp_preflight_stage_booters(
    const nvx_gsp_bundle_t *bundle,
    uint32_t load_fuse_version, uint8_t *load_staging, size_t load_capacity,
    uint32_t unload_fuse_version, uint8_t *unload_staging, size_t unload_capacity,
    nvx_gsp_image_info_t *out_load_info, nvx_gsp_image_info_t *out_unload_info);
#endif
