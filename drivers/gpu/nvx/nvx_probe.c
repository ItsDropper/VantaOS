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
#define NVX_PMC_BOOT_42_OFFSET 0x0A00U
#define NVX_PMC_BOOT_REQUIRED_LENGTH 0x0008U
#define NVX_PMC_BOOT_42_REQUIRED_LENGTH (NVX_PMC_BOOT_42_OFFSET + sizeof(uint32_t))

/* NVIDIA OpenRM nv_ref.h: PMC_BOOT_0 fields. */
#define NVX_PMC_BOOT_0_MINOR_MASK 0x0000000FU
#define NVX_PMC_BOOT_0_MAJOR_MASK 0x000000F0U
#define NVX_PMC_BOOT_0_ARCH1_MASK 0x00000100U
#define NVX_PMC_BOOT_0_IMPL_MASK  0x00F00000U
#define NVX_PMC_BOOT_0_ARCH0_MASK 0x1F000000U
/* PMC_BOOT_42 exposes a contiguous 6-bit architecture field. */
#define NVX_PMC_BOOT_42_ARCH_MASK 0x3F000000U
#define NVX_PMC_BOOT_42_IMPL_MASK 0x00F00000U

nvx_probe_status_t nvx_probe_mapped_bar0(
    const struct pci_device *device,
    volatile uint32_t *bar0,
    size_t mapped_length,
    nvx_register_snapshot_t *out_snapshot)
{
    nvx_mmio_region_t region;
    uint32_t boot0;
    uint32_t boot1;
    uint32_t boot42 = 0U;

    if (out_snapshot == 0)
        return NVX_PROBE_INVALID_ARGUMENT;
    out_snapshot->pmc_boot_0 = 0U;
    out_snapshot->pmc_boot_1 = 0U;
    out_snapshot->pmc_boot_42 = 0U;
    out_snapshot->pci_device_id = 0U;
    out_snapshot->identified_family = NVX_FAMILY_UNKNOWN;
    out_snapshot->identified_profile = NVX_PROFILE_UNKNOWN;
    out_snapshot->architecture = 0U;
    out_snapshot->implementation = 0U;
    out_snapshot->major_revision = 0U;
    out_snapshot->minor_revision = 0U;
    out_snapshot->extended_architecture = 0U;
    out_snapshot->extended_implementation = 0U;
    out_snapshot->extended_revision_valid = 0U;

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

    if (nvx_mmio_read32(&region, NVX_PMC_BOOT_0_OFFSET, &boot0) != NVX_MMIO_OK ||
        nvx_mmio_read32(&region, NVX_PMC_BOOT_1_OFFSET, &boot1) != NVX_MMIO_OK)
        return NVX_PROBE_REGISTER_ACCESS_FAILED;

    if (boot0 == 0xFFFFFFFFU || boot1 == 0xFFFFFFFFU ||
        (boot0 == 0U && boot1 == 0U))
        return NVX_PROBE_UNRESPONSIVE_REGISTERS;

    out_snapshot->pmc_boot_0 = boot0;
    out_snapshot->pmc_boot_1 = boot1;
    out_snapshot->pci_device_id = device->device_id;
    out_snapshot->architecture =
        (uint8_t)(((boot0 & NVX_PMC_BOOT_0_ARCH0_MASK) >> 24U) |
                  ((boot0 & NVX_PMC_BOOT_0_ARCH1_MASK) >> 3U));
    out_snapshot->implementation =
        (uint8_t)((boot0 & NVX_PMC_BOOT_0_IMPL_MASK) >> 20U);
    out_snapshot->major_revision =
        (uint8_t)((boot0 & NVX_PMC_BOOT_0_MAJOR_MASK) >> 4U);
    out_snapshot->minor_revision = (uint8_t)(boot0 & NVX_PMC_BOOT_0_MINOR_MASK);

    /*
     * BOOT_42 is optional. Do not fail a usable BOOT_0/1 probe when the BAR
     * extent is too short or the optional register reads as a reserved/all-ones
     * value on an older generation. Never write this register.
     */
    if (mapped_length >= NVX_PMC_BOOT_42_REQUIRED_LENGTH &&
        nvx_mmio_read32(&region, NVX_PMC_BOOT_42_OFFSET, &boot42) == NVX_MMIO_OK &&
        boot42 != 0U && boot42 != 0xFFFFFFFFU) {
        out_snapshot->pmc_boot_42 = boot42;
        out_snapshot->extended_architecture =
            (uint8_t)((boot42 & NVX_PMC_BOOT_42_ARCH_MASK) >> 24U);
        out_snapshot->extended_implementation =
            (uint8_t)((boot42 & NVX_PMC_BOOT_42_IMPL_MASK) >> 20U);
        out_snapshot->extended_revision_valid = 1U;
    }

    (void)nvx_identify_pci_device(device->device_id,
                                  &out_snapshot->identified_family,
                                  &out_snapshot->identified_profile);
    return NVX_PROBE_OK;
}
