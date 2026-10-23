/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris.h"
#include "intel_gen9.h"

#include <assert.h>
#include <string.h>

uint8_t pci_read_config8(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    (void)offset;
    return 0U;
}

uint32_t pci_read_config32(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    (void)offset;
    return 0U;
}

int pci_get_bar(const struct pci_device *device, uint8_t index,
                uint64_t *address, int *is_io, int *is_64bit)
{
    (void)device;
    (void)index;
    if (address != NULL) *address = 0U;
    if (is_io != NULL) *is_io = 0;
    if (is_64bit != NULL) *is_64bit = 0;
    return 0;
}

static gpu_pci_resource_inventory_t resources_with_bars(void)
{
    gpu_pci_resource_inventory_t resources;
    memset(&resources, 0, sizeof(resources));
    resources.primary_mmio_index = GPU_PCI_BAR_NONE;
    resources.bars[0] = (gpu_pci_bar_resource_t) {
        .base = 0x90000000ULL, .index = 0U, .is_io = 0U,
        .is_64bit = 1U, .prefetchable = 0U
    };
    resources.bars[1] = (gpu_pci_bar_resource_t) {
        .base = 0xA0000000ULL, .index = 2U, .is_io = 0U,
        .is_64bit = 0U, .prefetchable = 0U
    };
    resources.count = 2U;
    return resources;
}

int main(void)
{
    gpu_pci_resource_inventory_t resources = resources_with_bars();
    intel_gen9_profile_t intel;
    amd_polaris_profile_t amd;

    assert(intel_gen9_match_id(0x8086U, 0x1912U));
    assert(intel_gen9_match_id(0x8086U, 0x1916U));
    assert(!intel_gen9_match_id(0x8086U, 0x56A0U));
    assert(!intel_gen9_match_id(0x1002U, 0x1912U));
    assert(intel_gen9_inspect(0x8086U, 0x1912U, &resources, &intel) ==
           INTEL_GEN9_OK);
    assert(intel.generation == 9U && intel.gttmmadr_base == 0x90000000ULL);
    assert(intel.requires_guc_firmware && intel.huc_firmware_media_only);
    assert(!intel.acceleration_ready);

    resources.bars[0].base = 0U;
    assert(intel_gen9_inspect(0x8086U, 0x1912U, &resources, &intel) ==
           INTEL_GEN9_NO_ASSIGNED_GTTMMADR);
    resources.bars[0].base = 0x90000800ULL;
    assert(intel_gen9_inspect(0x8086U, 0x1912U, &resources, &intel) ==
           INTEL_GEN9_INVALID_GTTMMADR);
    assert(intel_gen9_inspect(0x8086U, 0xFFFFU, &resources, &intel) ==
           INTEL_GEN9_UNSUPPORTED_DEVICE);
    assert(intel_gen9_inspect(0x8086U, 0x1912U, NULL, &intel) ==
           INTEL_GEN9_INVALID_ARGUMENT);

    resources = resources_with_bars();
    assert(amd_polaris_match_id(0x1002U, 0x67DFU));
    assert(amd_polaris_match_id(0x1002U, 0x67C0U));
    assert(!amd_polaris_match_id(0x1002U, 0x744CU));
    assert(!amd_polaris_match_id(0x8086U, 0x67DFU));
    assert(amd_polaris_inspect(0x1002U, 0x67DFU, &resources, &amd) ==
           AMD_POLARIS_OK);
    assert(amd.architecture_gcn_major == 4U);
    assert(amd.mmio_base == 0xA0000000ULL);
    assert(amd.requires_psp_firmware && amd.requires_sdma_firmware &&
           amd.requires_gfx_firmware);
    assert(!amd.acceleration_ready);

    resources.bars[1].base = 0U;
    assert(amd_polaris_inspect(0x1002U, 0x67DFU, &resources, &amd) ==
           AMD_POLARIS_NO_ASSIGNED_MMIO);
    resources.bars[1].base = 0xA0000800ULL;
    assert(amd_polaris_inspect(0x1002U, 0x67DFU, &resources, &amd) ==
           AMD_POLARIS_INVALID_MMIO);
    assert(amd_polaris_inspect(0x1002U, 0x744CU, &resources, &amd) ==
           AMD_POLARIS_UNSUPPORTED_DEVICE);
    assert(amd_polaris_inspect(0x1002U, 0x67DFU, NULL, &amd) ==
           AMD_POLARIS_INVALID_ARGUMENT);
    return 0;
}
