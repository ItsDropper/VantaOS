/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_BOOT_PACKAGE_H
#define VANTA_NVX_GSP_BOOT_PACKAGE_H

#include "nvx_gsp_boot_memory.h"
#include "nvx_firmware.h"

typedef enum nvx_gsp_boot_package_status {
    NVX_GSP_BOOT_PACKAGE_OK = 0,
    NVX_GSP_BOOT_PACKAGE_INVALID_ARGUMENT,
    NVX_GSP_BOOT_PACKAGE_FIRMWARE_FAILED,
    NVX_GSP_BOOT_PACKAGE_MEMORY_FAILED,
    NVX_GSP_BOOT_PACKAGE_BOOTER_STAGE_FAILED,
    NVX_GSP_BOOT_PACKAGE_WPR_FAILED,
    NVX_GSP_BOOT_PACKAGE_BUSY
} nvx_gsp_boot_package_status_t;

/*
 * Owns the acquired firmware bundle and all SYSRAM DMA artifacts needed by
 * the GA10x SEC2/GSP bootstrap. The framebuffer/WPR offsets remain platform
 * inputs: this layer never invents framebuffer size or heap policy.
 */
typedef struct nvx_gsp_boot_package {
    nvx_gsp_bundle_t firmware;
    nvx_gsp_boot_memory_t memory;
    uint64_t wpr_metadata_phys;
    uint8_t prepared;
} nvx_gsp_boot_package_t;

/*
 * Acquire and verify all five GA10x firmware components, stage fuse-selected
 * SEC2 booters, bind allocated SYSRAM addresses into a platform-planned WPR
 * layout, and materialize WPR metadata. Only read-only fuse MMIO is performed to select
 * the signed booter payload; this preparation does not start either Falcon.
 *
 * Zero-initialize package before first use. On failure, acquired resources are
 * unwound. out_layout receives the finalized layout only on success.
 */
nvx_gsp_boot_package_status_t nvx_gsp_boot_package_prepare(
    const nvx_firmware_provider_t *provider, const char *chip,
    const char *version, const nvx_mmio_region_t *bar0,
    const nvx_gsp_wpr_layout_t *platform_layout,
    nvx_gsp_boot_package_t *package,
    nvx_gsp_wpr_layout_t *out_layout);

/*
 * Release only after all DMA ownership has returned to the CPU. If any device
 * still owns a boot artifact, BUSY is returned and the complete package stays
 * intact for recovery/retry.
 */
nvx_gsp_boot_package_status_t nvx_gsp_boot_package_release(
    nvx_gsp_boot_package_t *package);

#endif
