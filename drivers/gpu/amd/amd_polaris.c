/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris.h"

#include <string.h>

int amd_polaris_match_id(uint16_t vendor_id, uint16_t device_id)
{
    return vendor_id == AMD_POLARIS_VENDOR_ID &&
           (device_id == AMD_POLARIS_10_DEVICE_ID ||
            device_id == AMD_POLARIS_10_ALT_DEVICE_ID);
}

amd_polaris_status_t amd_polaris_inspect(
    uint16_t vendor_id, uint16_t device_id,
    const gpu_pci_resource_inventory_t *resources,
    amd_polaris_profile_t *out_profile)
{
    const gpu_pci_bar_resource_t *bar;

    if (resources == NULL || out_profile == NULL)
        return AMD_POLARIS_INVALID_ARGUMENT;
    memset(out_profile, 0, sizeof(*out_profile));
    if (!amd_polaris_match_id(vendor_id, device_id))
        return AMD_POLARIS_UNSUPPORTED_DEVICE;

    out_profile->device_id = device_id;
    out_profile->architecture_gcn_major = 4U;
    out_profile->architecture_gcn_minor = 0U;
    out_profile->requires_psp_firmware = 1U;
    out_profile->requires_sdma_firmware = 1U;
    out_profile->requires_gfx_firmware = 1U;
    out_profile->acceleration_ready = 0U;

    bar = gpu_pci_resources_find_mmio(resources, AMD_POLARIS_MMIO_BAR);
    if (bar == NULL || bar->base == 0U)
        return AMD_POLARIS_NO_ASSIGNED_MMIO;
    if (bar->is_io != 0U || (bar->base & 0xFFFU) != 0U)
        return AMD_POLARIS_INVALID_MMIO;

    out_profile->mmio_base = bar->base;
    return AMD_POLARIS_OK;
}
