/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "nvx_device.h"
#include "nvx_generation.h"

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
        .bus = 1, .slot = 2, .function = 0,
        .vendor_id = NVX_PCI_VENDOR_NVIDIA, .device_id = 0x1234,
        .class_code = 0x03, .subclass = 0x02, .prog_if = 0, .revision = 7
    };
    nvx_device_descriptor_t descriptor;
    nvx_generation_family_t family;
    nvx_device_profile_t profile;
    static const uint16_t supported_ids[] = {
        0x2503U, 0x2504U, 0x2507U, 0x2508U, 0x2509U, 0x2544U, 0x2548U
    };
    for (unsigned i = 0U; i < sizeof(supported_ids) / sizeof(supported_ids[0]); ++i) {
        const nvx_generation_descriptor_t *generation =
            nvx_generation_for_pci(supported_ids[i]);
        assert(generation != 0);
        assert(generation->family == NVX_FAMILY_AMPERE);
        assert(generation->profile == NVX_PROFILE_GEFORCE_RTX_3060);
        assert(generation->boot_sequence == NVX_BOOT_SEQUENCE_GA10X_SEC2_GSP);
        assert(generation->pmc_boot_0_offset == 0U);
        assert(generation->pmc_boot_1_offset == 4U);
        assert(generation->firmware_component_count == 5U);
    }
    assert(nvx_generation_for_pci(0x1234U) == 0);
    assert(nvx_generation_for_pci(0x2C02U) == 0);
    assert(nvx_generation_for_pci(0x2C05U) == 0);
    assert(nvx_generation_for_pci(0x2C07U) == 0);
    const nvx_generation_descriptor_t *ga106 =
        nvx_generation_for_firmware_chip("ga106");
    assert(ga106 != 0);
    assert(ga106->family == NVX_FAMILY_AMPERE);
    assert(ga106->profile == NVX_PROFILE_GEFORCE_RTX_3060);
    assert(ga106->firmware_directory != 0);
    assert(ga106->firmware_directory[0] != '\\0');
    assert(ga106->firmware_component_count == NVX_GENERATION_FIRMWARE_COMPONENTS);
    assert(ga106->firmware_components != 0);
    for (size_t i = 0U; i < ga106->firmware_component_count; ++i)
        assert(ga106->firmware_components[i] != 0 &&
               ga106->firmware_components[i][0] != '\\0');
    assert(nvx_generation_for_firmware_chip("gb202") == 0);
    assert(nvx_generation_for_firmware_chip(0) == 0);
    assert(nvx_generation_for_firmware_chip("") == 0);
    assert(nvx_generation_for_firmware_chip("GA106") == 0);
    assert(nvx_generation_for_firmware_chip("ga106-extra") == 0);
    assert(nvx_generation_for_firmware_chip("xga106") == 0);
    assert(!nvx_generation_supports_firmware_chip(0));
    assert(!nvx_generation_supports_firmware_chip("gb202"));
    assert(nvx_generation_supports_firmware_chip("ga107"));

    assert(!nvx_match_pci_device(0));
    device.vendor_id = 0x8086;
    assert(!nvx_match_pci_device(&device));
    device.vendor_id = NVX_PCI_VENDOR_NVIDIA;
    device.class_code = 0x04;
    assert(!nvx_match_pci_device(&device));
    assert(!nvx_describe_pci_device(&device, &descriptor));
    assert(!nvx_describe_pci_device(0, &descriptor));
    assert(!nvx_describe_pci_device(&device, 0));

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

    assert(nvx_identify_architecture(0x16U, &family));
    assert(family == NVX_FAMILY_TURING);
    assert(nvx_identify_architecture(0x17U, &family));
    assert(family == NVX_FAMILY_AMPERE);
    assert(nvx_identify_architecture(0x18U, &family));
    assert(family == NVX_FAMILY_HOPPER);
    assert(nvx_identify_architecture(0x19U, &family));
    assert(family == NVX_FAMILY_ADA);
    assert(nvx_identify_architecture(0x1AU, &family));
    assert(family == NVX_FAMILY_BLACKWELL);
    assert(nvx_identify_architecture(0x1BU, &family));
    assert(family == NVX_FAMILY_BLACKWELL);
    assert(nvx_identify_architecture(0x1CU, &family));
    assert(family == NVX_FAMILY_RUBIN);
    assert(!nvx_identify_architecture(0x3FU, &family));
    assert(family == NVX_FAMILY_UNKNOWN);
    assert(!nvx_identify_architecture(0x17U, 0));

    device.class_code = 0x03;
    device.subclass = 0x80;
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
