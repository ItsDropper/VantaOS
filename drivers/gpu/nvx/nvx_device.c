/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "nvx_device.h"

/*
 * PCI class 03h identifies display controllers. Class 03h subclasses 00h
 * (VGA-compatible) and 02h (3D controller) are GPU candidates. Vendor ID alone
 * is insufficient: NVIDIA also ships audio and other PCI functions.
 */
int nvx_match_pci_device(const struct pci_device *device)
{
    if (device == 0 || device->vendor_id != NVX_PCI_VENDOR_NVIDIA)
        return 0;

    if (device->class_code != 0x03U)
        return 0;

    return device->subclass == 0x00U || device->subclass == 0x02U;
}

/*
 * Describe PCI identity only. Resource assignment and sizing are intentionally
 * absent: a raw BAR address is not a validated MMIO mapping or a resource
 * length. Leave MMIO fields zero until a separate safe resource API exists.
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

    out_descriptor->family = NVX_FAMILY_UNKNOWN;
    out_descriptor->architecture_id = 0;
    out_descriptor->implementation_id = 0;
    out_descriptor->mmio_base = 0;
    out_descriptor->mmio_length = 0;
    out_descriptor->is_64bit_bar = 0;
    return 1;
}
