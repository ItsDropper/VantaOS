/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_PREFLIGHT_H
#define VANTA_NVX_GSP_PREFLIGHT_H
#include <stdint.h>
#include "nvx_gsp_elf.h"
#define NVX_GSP_GA10X_SIGNATURE_SECTION ".fwsignature_ga10x"
typedef enum nvx_gsp_preflight_status {
    NVX_GSP_PREFLIGHT_OK=0,
    NVX_GSP_PREFLIGHT_INVALID_ARGUMENT,
    NVX_GSP_PREFLIGHT_BUNDLE_NOT_READY,
    NVX_GSP_PREFLIGHT_COMPONENT_INVALID,
    NVX_GSP_PREFLIGHT_RUNTIME_INVALID,
    NVX_GSP_PREFLIGHT_CONTAINER_INVALID,
    NVX_GSP_PREFLIGHT_VERSION_MISMATCH
} nvx_gsp_preflight_status_t;
typedef struct nvx_gsp_preflight_report {
    nvx_gsp_elf_info_t runtime;
    nvx_gsp_elf_section_t firmware_image;
    nvx_gsp_elf_section_t firmware_version;
    char version[64];
    uint64_t runtime_bytes;
    uint64_t ucode_archive_bytes;
    uint64_t log_bytes;
    uint8_t runtime_verified;
    uint8_t ucode_archive_verified;
    uint8_t optional_log_present;
} nvx_gsp_preflight_report_t;
/*
 * Structural gate before any future boot path. Bundle acquisition must already
 * have passed the platform verification callback. This validates the GA10x
 * RISC-V runtime ELF, .fwimage, .fwversion and signature sections, including
 * exact firmware-version match. It does not cryptographically verify signatures,
 * parse the ucode archive, allocate WPR memory, or boot GSP.
 */
nvx_gsp_preflight_status_t nvx_gsp_preflight_validate(
    const nvx_gsp_bundle_t *,nvx_gsp_preflight_report_t *out_report);
#endif
