/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "nvx_device.h"

/*
 * Keep this table deliberately small and auditable. These device IDs are
 * already identified as RTX 3060 variants in VantaOS's PCI device-name table.
 * The result identifies a generation/profile only; it does not imply that
 * initialization, firmware, scanout, or acceleration is implemented.
 */
static const uint16_t nvx_ampere_rtx3060_ids[] = {
    0x2503U, 0x2504U, 0x2507U, 0x2508U, 0x2509U, 0x2544U, 0x2548U
};

/*
 * PCI class 03h identifies display controllers. Keep subclass matching broad:
 * NVIDIA devices can expose different display-controller subclasses. Vendor ID
 * alone is insufficient because NVIDIA also ships audio and other PCI functions.
 */
int nvx_match_pci_device(const struct pci_device *device)
{
    return device != 0 &&
           device->vendor_id == NVX_PCI_VENDOR_NVIDIA &&
           device->class_code == 0x03U;
}

int nvx_identify_pci_device(uint16_t device_id,
                            nvx_generation_family_t *out_family,
                            nvx_device_profile_t *out_profile)
{
    if (out_family == 0 || out_profile == 0)
        return 0;

    *out_family = NVX_FAMILY_UNKNOWN;
    *out_profile = NVX_PROFILE_UNKNOWN;

    for (unsigned i = 0;
         i < sizeof(nvx_ampere_rtx3060_ids) / sizeof(nvx_ampere_rtx3060_ids[0]);
         ++i)
    {
        if (nvx_ampere_rtx3060_ids[i] == device_id)
        {
            *out_family = NVX_FAMILY_AMPERE;
            *out_profile = NVX_PROFILE_GEFORCE_RTX_3060;
            return 1;
        }
    }

    return 0;
}

/*
 * Describe PCI identity and conservative family recognition only. Resource
 * assignment and sizing are intentionally absent: a raw BAR address is not a
 * validated MMIO mapping or a resource length.
 */
int nvx_describe_pci_device(const struct pci_device *device,
                            nvx_device_descriptor_t *out_descriptor)
{
    if (out_descriptor == 0 || !nvx_match_pci_device(device))
        return 0;

    out_descriptor->identity.vendor_id = device->vendor_id;
    out_descriptor->identity.device_id = device->device_id;
    out_descriptor->identity.subsystem_vendor_id = 0;
    out_descriptor->identity.subsystem_device_id = 0;
    out_descriptor->identity.revision_id = device->revision;
    out_descriptor->identity.class_code = device->class_code;
    out_descriptor->identity.subclass = device->subclass;
    out_descriptor->identity.programming_interface = device->prog_if;

    if ((pci_read_config8(device, 0x0EU) & 0x7FU) == 0U)
    {
        out_descriptor->identity.subsystem_vendor_id =
            pci_read_config16(device, 0x2CU);
        out_descriptor->identity.subsystem_device_id =
            pci_read_config16(device, 0x2EU);
    }

    (void)nvx_identify_pci_device(device->device_id,
                                  &out_descriptor->family,
                                  &out_descriptor->profile);
    /* Internal architecture/implementation IDs remain unset pending validation. */
    out_descriptor->architecture_id = 0;
    out_descriptor->implementation_id = 0;
    out_descriptor->mmio_base = 0;
    out_descriptor->mmio_length = 0;
    out_descriptor->is_64bit_bar = 0;
    return 1;
}
