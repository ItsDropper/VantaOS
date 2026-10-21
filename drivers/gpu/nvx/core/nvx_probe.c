/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_probe.h"

#include "nvx_lifecycle.h"
#include "nvx_mmio.h"
#include "nvx_generation.h"

#include <stdint.h>


/* NVIDIA OpenRM nv_ref.h register fields. */
#define NVX_PMC_BOOT_0_MINOR_MASK 0x0000000FU
#define NVX_PMC_BOOT_0_MAJOR_MASK 0x000000F0U
#define NVX_PMC_BOOT_0_ARCH1_MASK 0x00000100U
#define NVX_PMC_BOOT_0_IMPL_MASK  0x00F00000U
#define NVX_PMC_BOOT_0_ARCH0_MASK 0x1F000000U
#define NVX_PMC_BOOT_42_ARCH_MASK 0x3F000000U
#define NVX_PMC_BOOT_42_CHIP_ID_MASK 0x3FF00000U
#define NVX_PMC_BOOT_42_IMPL_MASK 0x00F00000U
#define NVX_PMC_BOOT_42_MAJOR_MASK 0x000F0000U
#define NVX_PMC_BOOT_42_MINOR_MASK 0x0000F000U
#define NVX_PMC_BOOT_42_MINOR_EXTRA_MASK 0x00000F00U

static void nvx_snapshot_clear(nvx_register_snapshot_t *snapshot)
{
    snapshot->pmc_boot_0 = 0U;
    snapshot->pmc_boot_1 = 0U;
    snapshot->pmc_boot_42 = 0U;
    snapshot->pci_device_id = 0U;
    snapshot->extended_chip_id = 0U;
    snapshot->identified_family = NVX_FAMILY_UNKNOWN;
    snapshot->identified_profile = NVX_PROFILE_UNKNOWN;
    snapshot->architecture = 0U;
    snapshot->implementation = 0U;
    snapshot->major_revision = 0U;
    snapshot->minor_revision = 0U;
    snapshot->extended_architecture = 0U;
    snapshot->extended_implementation = 0U;
    snapshot->extended_major_revision = 0U;
    snapshot->extended_minor_revision = 0U;
    snapshot->extended_minor_revision_extra = 0U;
    snapshot->extended_revision_valid = 0U;
    snapshot->identity_mismatch = 0U;
}

nvx_probe_status_t nvx_probe_mapped_bar0(
    const struct pci_device *device,
    volatile uint32_t *bar0,
    size_t mapped_length,
    nvx_register_snapshot_t *out_snapshot)
{
    nvx_mmio_region_t region;
    const nvx_generation_descriptor_t *generation;
    uint32_t boot0;
    uint32_t boot1;
    uint32_t boot42 = 0U;
    nvx_generation_family_t pci_family = NVX_FAMILY_UNKNOWN;
    nvx_device_profile_t pci_profile = NVX_PROFILE_UNKNOWN;
    nvx_generation_family_t boot0_family = NVX_FAMILY_UNKNOWN;

    if (out_snapshot == 0)
        return NVX_PROBE_INVALID_ARGUMENT;
    nvx_snapshot_clear(out_snapshot);

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
        out_snapshot->extended_chip_id =
            (uint16_t)((boot42 & NVX_PMC_BOOT_42_CHIP_ID_MASK) >> 20U);
        out_snapshot->extended_implementation =
            (uint8_t)((boot42 & NVX_PMC_BOOT_42_IMPL_MASK) >> 20U);
        out_snapshot->extended_major_revision =
            (uint8_t)((boot42 & NVX_PMC_BOOT_42_MAJOR_MASK) >> 16U);
        out_snapshot->extended_minor_revision =
            (uint8_t)((boot42 & NVX_PMC_BOOT_42_MINOR_MASK) >> 12U);
        out_snapshot->extended_minor_revision_extra =
            (uint8_t)((boot42 & NVX_PMC_BOOT_42_MINOR_EXTRA_MASK) >> 8U);
        out_snapshot->extended_revision_valid = 1U;
    }

    (void)nvx_identify_pci_device(device->device_id, &pci_family, &pci_profile);
    if (nvx_identify_architecture(out_snapshot->architecture, &boot0_family)) {
        out_snapshot->identified_family = boot0_family;
        if (pci_family != NVX_FAMILY_UNKNOWN && pci_family != boot0_family)
            out_snapshot->identity_mismatch = 1U;
    } else {
        /* Keep PCI matching as a candidate hint, not as a claimed register ID. */
        out_snapshot->identified_family = pci_family;
    }
    if (pci_family == out_snapshot->identified_family)
        out_snapshot->identified_profile = pci_profile;

    if (out_snapshot->extended_revision_valid) {
        if (out_snapshot->extended_architecture != out_snapshot->architecture)
            out_snapshot->identity_mismatch = 1U;
        if (boot0_family != NVX_FAMILY_UNKNOWN) {
            nvx_generation_family_t boot42_family = NVX_FAMILY_UNKNOWN;
            if (nvx_identify_architecture(out_snapshot->extended_architecture,
                                          &boot42_family) &&
                boot42_family != boot0_family)
                out_snapshot->identity_mismatch = 1U;
        }
    }
    return NVX_PROBE_OK;
}
