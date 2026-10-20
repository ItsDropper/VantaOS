/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "nvx_device.h"

#include <assert.h>
#include <stdint.h>

static uint8_t test_header_type;
static uint16_t test_subsystem_vendor;
static uint16_t test_subsystem_device;

uint8_t pci_read_config8(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    return offset == 0x0EU ? test_header_type : 0;
}

uint16_t pci_read_config16(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    if (offset == 0x04U)
        return NVX_PCI_COMMAND_MEMORY_SPACE;
    if (offset == 0x2CU)
        return test_subsystem_vendor;
    if (offset == 0x2EU)
        return test_subsystem_device;
    return 0xFFFFU;
}

int main(void)
{
    struct pci_device device = {
        .bus = 1,
        .slot = 2,
        .function = 0,
        .vendor_id = NVX_PCI_VENDOR_NVIDIA,
        .device_id = 0x1234,
        .class_code = 0x03,
        .subclass = 0x02,
        .prog_if = 0,
        .revision = 7
    };
    nvx_device_descriptor_t descriptor;
    nvx_generation_family_t family;
    nvx_device_profile_t profile;

    assert(!nvx_match_pci_device(0));

    device.vendor_id = 0x8086;
    assert(!nvx_match_pci_device(&device));

    device.vendor_id = NVX_PCI_VENDOR_NVIDIA;
    device.class_code = 0x04; /* NVIDIA audio function, not a display candidate. */
    assert(!nvx_match_pci_device(&device));
    assert(!nvx_describe_pci_device(&device, &descriptor));
    assert(!nvx_describe_pci_device(0, &descriptor));
    assert(!nvx_describe_pci_device(&device, 0));

    /* Only explicitly listed PCI IDs receive a family/profile classification. */
    assert(nvx_identify_pci_device(0x2503U, &family, &profile));
    assert(family == NVX_FAMILY_AMPERE);
    assert(profile == NVX_PROFILE_GEFORCE_RTX_3060);
    assert(nvx_identify_pci_device(0x2504U, &family, &profile));
    assert(family == NVX_FAMILY_AMPERE);
    assert(profile == NVX_PROFILE_GEFORCE_RTX_3060);
    assert(nvx_identify_pci_device(0x2548U, &family, &profile));
    assert(family == NVX_FAMILY_AMPERE);
    assert(profile == NVX_PROFILE_GEFORCE_RTX_3060);
    assert(!nvx_identify_pci_device(0x1234U, &family, &profile));
    assert(family == NVX_FAMILY_UNKNOWN);
    assert(profile == NVX_PROFILE_UNKNOWN);
    assert(!nvx_identify_pci_device(0x2503U, 0, &profile));
    assert(!nvx_identify_pci_device(0x2503U, &family, 0));

    device.class_code = 0x03;
    device.subclass = 0x80; /* Do not overfit matching to one display subclass. */
    test_header_type = 0;
    test_subsystem_vendor = 0x1043;
    test_subsystem_device = 0x88A1;
    assert(nvx_describe_pci_device(&device, &descriptor));

    assert(descriptor.identity.vendor_id == NVX_PCI_VENDOR_NVIDIA);
    assert(descriptor.identity.device_id == 0x1234);
    assert(descriptor.identity.subsystem_vendor_id == 0x1043);
    assert(descriptor.identity.subsystem_device_id == 0x88A1);
    assert(descriptor.identity.revision_id == 7);
    assert(descriptor.identity.class_code == 0x03);
    assert(descriptor.identity.subclass == 0x80);
    assert(descriptor.family == NVX_FAMILY_UNKNOWN);
    assert(descriptor.profile == NVX_PROFILE_UNKNOWN);
    assert(descriptor.pci_command == NVX_PCI_COMMAND_MEMORY_SPACE);
    assert((descriptor.pci_command & NVX_PCI_COMMAND_BUS_MASTER) == 0);
    assert(descriptor.architecture_id == 0);
    assert(descriptor.implementation_id == 0);
    assert(descriptor.mmio_base == 0);
    assert(descriptor.mmio_length == 0);
    assert(descriptor.is_64bit_bar == 0);

    device.device_id = 0x2503;
    assert(nvx_describe_pci_device(&device, &descriptor));
    assert(descriptor.family == NVX_FAMILY_AMPERE);
    assert(descriptor.profile == NVX_PROFILE_GEFORCE_RTX_3060);
    assert(descriptor.architecture_id == 0);
    assert(descriptor.implementation_id == 0);

    test_header_type = 1;
    assert(nvx_describe_pci_device(&device, &descriptor));
    assert(descriptor.identity.subsystem_vendor_id == 0);
    assert(descriptor.identity.subsystem_device_id == 0);

    return 0;
}
