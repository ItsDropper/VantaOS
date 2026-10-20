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
 * The result identifies a PCI candidate only; it does not imply initialization.
 */
static const uint16_t nvx_ampere_rtx3060_ids[] = {
    0x2503U, 0x2504U, 0x2507U, 0x2508U, 0x2509U, 0x2544U, 0x2548U
};

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
         ++i) {
        if (nvx_ampere_rtx3060_ids[i] == device_id) {
            *out_family = NVX_FAMILY_AMPERE;
            *out_profile = NVX_PROFILE_GEFORCE_RTX_3060;
            return 1;
        }
    }
    return 0;
}

/*
 * Architecture codes are taken from NVIDIA's published NV_PMC_BOOT_0 and
 * NV_PMC_BOOT_42 definitions. Several older families share a coarse legacy
 * bucket because this driver has no generation-specific implementation for
 * them yet. Unlisted codes deliberately return UNKNOWN.
 */
int nvx_identify_architecture(uint8_t architecture,
                              nvx_generation_family_t *out_family)
{
    if (out_family == 0)
        return 0;

    switch (architecture) {
    case 0x11U: /* Maxwell GM100 */
    case 0x12U: /* Maxwell GM200 */
    case 0x13U: /* Pascal GP100 */
    case 0x14U: /* Volta GV100 */
    case 0x15U: /* Volta GV110 */
        *out_family = NVX_FAMILY_LEGACY;
        return 1;
    case 0x16U: /* Turing TU100 */
        *out_family = NVX_FAMILY_TURING;
        return 1;
    case 0x17U: /* Ampere GA100 / GA10x */
        *out_family = NVX_FAMILY_AMPERE;
        return 1;
    case 0x18U: /* Hopper GH100 */
        *out_family = NVX_FAMILY_HOPPER;
        return 1;
    case 0x19U: /* Ada AD100 */
        *out_family = NVX_FAMILY_ADA;
        return 1;
    case 0x1AU: /* Blackwell GB100 */
    case 0x1BU: /* Blackwell GB200 */
        *out_family = NVX_FAMILY_BLACKWELL;
        return 1;
    case 0x1CU: /* Rubin GR100 */
        *out_family = NVX_FAMILY_RUBIN;
        return 1;
    default:
        *out_family = NVX_FAMILY_UNKNOWN;
        return 0;
    }
}

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
    out_descriptor->pci_command = pci_read_config16(device, 0x04U);

    if ((pci_read_config8(device, 0x0EU) & 0x7FU) == 0U) {
        out_descriptor->identity.subsystem_vendor_id =
            pci_read_config16(device, 0x2CU);
        out_descriptor->identity.subsystem_device_id =
            pci_read_config16(device, 0x2EU);
    }

    (void)nvx_identify_pci_device(device->device_id,
                                  &out_descriptor->family,
                                  &out_descriptor->profile);
    out_descriptor->architecture_id = 0;
    out_descriptor->implementation_id = 0;
    out_descriptor->mmio_base = 0;
    out_descriptor->mmio_length = 0;
    out_descriptor->is_64bit_bar = 0;
    return 1;
}
