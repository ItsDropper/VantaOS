/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_probe.h"

#include "nvx_lifecycle.h"
#include "nvx_mmio.h"

#include <stdint.h>

#define NVX_PMC_BOOT_0_OFFSET 0x0000U
#define NVX_PMC_BOOT_1_OFFSET 0x0004U
#define NVX_PMC_BOOT_REQUIRED_LENGTH 0x0008U

nvx_probe_status_t nvx_probe_mapped_bar0(
    const struct pci_device *device,
    volatile uint32_t *bar0,
    size_t mapped_length,
    nvx_register_snapshot_t *out_snapshot)
{
    nvx_mmio_region_t region;
    uint32_t boot0;
    uint32_t boot1;

    if (out_snapshot == 0)
        return NVX_PROBE_INVALID_ARGUMENT;
    out_snapshot->pmc_boot_0 = 0;
    out_snapshot->pmc_boot_1 = 0;
    out_snapshot->pci_device_id = 0;
    out_snapshot->identified_family = NVX_FAMILY_UNKNOWN;
    out_snapshot->identified_profile = NVX_PROFILE_UNKNOWN;

    if (device == 0 || bar0 == 0)
        return NVX_PROBE_INVALID_ARGUMENT;
    if (!nvx_match_pci_device(device))
        return NVX_PROBE_NOT_NVIDIA_DISPLAY;
    if ((pci_read_config16(device, 0x04U) &
         NVX_PCI_COMMAND_MEMORY_SPACE) == 0U)
        return NVX_PROBE_MEMORY_DECODE_DISABLED;
    if (mapped_length < NVX_PMC_BOOT_REQUIRED_LENGTH)
        return NVX_PROBE_BAR_TOO_SMALL;
    if (((uintptr_t)bar0 & (sizeof(uint32_t) - 1U)) != 0U)
        return NVX_PROBE_BAD_MAPPING_ALIGNMENT;

    region.base = (volatile uint8_t *)(void *)bar0;
    region.length = mapped_length;

    /*
     * Use the shared bounded accessor for every register read. Volatile loads
     * are required for device registers; unknown values are preserved rather
     * than interpreted as undocumented architecture IDs.
     */
    if (nvx_mmio_read32(&region, NVX_PMC_BOOT_0_OFFSET, &boot0) != NVX_MMIO_OK ||
        nvx_mmio_read32(&region, NVX_PMC_BOOT_1_OFFSET, &boot1) != NVX_MMIO_OK)
        return NVX_PROBE_REGISTER_ACCESS_FAILED;

    if (boot0 == 0xFFFFFFFFU || boot1 == 0xFFFFFFFFU ||
        (boot0 == 0U && boot1 == 0U))
        return NVX_PROBE_UNRESPONSIVE_REGISTERS;

    out_snapshot->pmc_boot_0 = boot0;
    out_snapshot->pmc_boot_1 = boot1;
    out_snapshot->pci_device_id = device->device_id;
    (void)nvx_identify_pci_device(device->device_id,
                                  &out_snapshot->identified_family,
                                  &out_snapshot->identified_profile);
    return NVX_PROBE_OK;
}
