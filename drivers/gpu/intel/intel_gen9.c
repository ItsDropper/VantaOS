/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9.h"

#include <string.h>

int intel_gen9_match_id(uint16_t vendor_id, uint16_t device_id)
{
    return vendor_id == INTEL_GEN9_VENDOR_ID &&
           (device_id == INTEL_GEN9_SKL_GT2_DEVICE_ID ||
            device_id == INTEL_GEN9_SKL_GT2_MOBILE_DEVICE_ID);
}

intel_gen9_status_t intel_gen9_inspect(
    uint16_t vendor_id, uint16_t device_id,
    const gpu_pci_resource_inventory_t *resources,
    intel_gen9_profile_t *out_profile)
{
    const gpu_pci_bar_resource_t *bar;

    if (resources == NULL || out_profile == NULL)
        return INTEL_GEN9_INVALID_ARGUMENT;
    memset(out_profile, 0, sizeof(*out_profile));
    if (!intel_gen9_match_id(vendor_id, device_id))
        return INTEL_GEN9_UNSUPPORTED_DEVICE;

    out_profile->device_id = device_id;
    out_profile->generation = 9U;
    out_profile->requires_guc_firmware = 1U;
    out_profile->requires_huc_firmware = 1U;
    out_profile->requires_gttmmadr = 1U;
    out_profile->acceleration_ready = 0U;

    bar = gpu_pci_resources_find_mmio(resources, INTEL_GEN9_GTTMMADR_BAR);
    if (bar == NULL || bar->base == 0U)
        return INTEL_GEN9_NO_ASSIGNED_GTTMMADR;
    if (bar->is_io != 0U || bar->is_64bit == 0U ||
        (bar->base & 0xFFFFU) != 0U)
        return INTEL_GEN9_INVALID_GTTMMADR;

    out_profile->gttmmadr_base = bar->base;
    return INTEL_GEN9_OK;
}
