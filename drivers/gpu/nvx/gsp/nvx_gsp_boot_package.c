/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_boot_package.h"

#include <stdint.h>
#include <string.h>

static int overlaps(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;
    if (an == 0U || bn == 0U)
        return 0;
    if (a == 0 || b == 0 || x > UINTPTR_MAX - an || y > UINTPTR_MAX - bn)
        return 1;
    return x < y + bn && y < x + an;
}

static int package_is_empty(const nvx_gsp_boot_package_t *package)
{
    return package != 0 && package->prepared == 0U &&
        package->firmware.acquired == 0U &&
        package->memory.acquired == 0U &&
        package->memory.radix3_elf.acquired == 0U &&
        package->memory.bootloader_payload.acquired == 0U &&
        package->memory.signature.acquired == 0U &&
        package->memory.booter_load.acquired == 0U &&
        package->memory.booter_unload.acquired == 0U &&
        package->memory.wpr_metadata.acquired == 0U;
}

nvx_gsp_boot_package_status_t nvx_gsp_boot_package_release(
    nvx_gsp_boot_package_t *package)
{
    if (!package)
        return NVX_GSP_BOOT_PACKAGE_INVALID_ARGUMENT;

    nvx_gsp_boot_memory_release(&package->memory);
    if (package->memory.acquired ||
        package->memory.radix3_elf.acquired ||
        package->memory.bootloader_payload.acquired ||
        package->memory.signature.acquired ||
        package->memory.booter_load.acquired ||
        package->memory.booter_unload.acquired ||
        package->memory.wpr_metadata.acquired)
        return NVX_GSP_BOOT_PACKAGE_BUSY;

    nvx_gsp_bundle_release(&package->firmware);
    memset(package, 0, sizeof(*package));
    return NVX_GSP_BOOT_PACKAGE_OK;
}

nvx_gsp_boot_package_status_t nvx_gsp_boot_package_prepare(
    const nvx_firmware_provider_t *provider, const char *chip,
    const char *version, const nvx_mmio_region_t *bar0,
    const nvx_gsp_wpr_layout_t *platform_layout,
    nvx_gsp_boot_package_t *package,
    nvx_gsp_wpr_layout_t *out_layout)
{
    nvx_gsp_boot_package_status_t result;
    nvx_gsp_bundle_status_t bundle_status;
    nvx_gsp_boot_memory_status_t memory_status;
    nvx_gsp_wpr_layout_t layout;

    if (!provider || !chip || !version || !bar0 || !bar0->base ||
        !platform_layout || !package || !out_layout ||
        !package_is_empty(package) ||
        overlaps(package, sizeof(*package), out_layout, sizeof(*out_layout)) ||
        overlaps(package, sizeof(*package), platform_layout,
                 sizeof(*platform_layout)) ||
        overlaps(out_layout, sizeof(*out_layout), platform_layout,
                 sizeof(*platform_layout)) ||
        overlaps(out_layout, sizeof(*out_layout), bar0, sizeof(*bar0)))
        return NVX_GSP_BOOT_PACKAGE_INVALID_ARGUMENT;

    memset(package, 0, sizeof(*package));
    bundle_status = nvx_gsp_bundle_acquire(provider, NVX_FAMILY_AMPERE,
        chip, version, &package->firmware);
    if (bundle_status != NVX_GSP_BUNDLE_OK) {
        result = NVX_GSP_BOOT_PACKAGE_FIRMWARE_FAILED;
        goto fail;
    }

    memory_status = nvx_gsp_boot_memory_acquire(&package->firmware,
                                                &package->memory);
    if (memory_status != NVX_GSP_BOOT_MEMORY_OK) {
        result = NVX_GSP_BOOT_PACKAGE_MEMORY_FAILED;
        goto fail;
    }

    memory_status = nvx_gsp_boot_memory_stage_booters_from_bar0(
        &package->memory, &package->firmware, bar0);
    if (memory_status != NVX_GSP_BOOT_MEMORY_OK) {
        result = NVX_GSP_BOOT_PACKAGE_BOOTER_STAGE_FAILED;
        goto fail;
    }

    layout = *platform_layout;
    if (nvx_gsp_boot_memory_bind_wpr(&package->memory, &layout) !=
        NVX_GSP_BOOT_MEMORY_OK ||
        nvx_gsp_boot_memory_prepare_wpr_metadata(&package->memory,
            platform_layout, &package->wpr_metadata_phys) !=
            NVX_GSP_BOOT_MEMORY_OK) {
        result = NVX_GSP_BOOT_PACKAGE_WPR_FAILED;
        goto fail;
    }

    *out_layout = layout;
    package->prepared = 1U;
    return NVX_GSP_BOOT_PACKAGE_OK;

fail:
    (void)nvx_gsp_boot_package_release(package);
    return result;
}
