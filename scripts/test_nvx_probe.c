/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_probe.h"

#include <assert.h>
#include <stdint.h>

static uint16_t mock_command = NVX_PCI_COMMAND_MEMORY_SPACE;
uint16_t pci_read_config16(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    (void)offset;
    return mock_command;
}
uint8_t pci_read_config8(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    (void)offset;
    return 0;
}

int main(void)
{
    struct pci_device gpu = {
        .vendor_id = NVX_PCI_VENDOR_NVIDIA, .device_id = 0x2503U,
        .class_code = 0x03U
    };
    struct pci_device other = { .vendor_id = 0x1234U, .class_code = 0x03U };
    volatile uint32_t registers[0xA04U / sizeof(uint32_t)] = { 0U };
    nvx_register_snapshot_t snapshot;

    /* Ampere architecture code 0x17, as defined in NVIDIA's public register map. */
    registers[0] = 0x17000000U;
    registers[1] = 0x00000001U;
    registers[0xA00U / sizeof(uint32_t)] = 0x17000000U;
    assert(nvx_probe_mapped_bar0(&gpu, registers, sizeof(registers), &snapshot)
           == NVX_PROBE_OK);
    assert(snapshot.pmc_boot_0 == registers[0]);
    assert(snapshot.pmc_boot_1 == registers[1]);
    assert(snapshot.pmc_boot_42 == registers[0xA00U / sizeof(uint32_t)]);
    assert(snapshot.pci_device_id == 0x2503U);
    assert(snapshot.identified_family == NVX_FAMILY_AMPERE);
    assert(snapshot.identified_profile == NVX_PROFILE_GEFORCE_RTX_3060);
    assert(snapshot.architecture == 0x17U);
    assert(snapshot.implementation == 0U);
    assert(snapshot.extended_architecture == 0x17U);
    assert(snapshot.extended_chip_id == 0x170U);
    assert(snapshot.extended_implementation == 0U);
    assert(snapshot.extended_major_revision == 0U);
    assert(snapshot.extended_minor_revision == 0U);
    assert(snapshot.extended_minor_revision_extra == 0U);
    assert(snapshot.extended_revision_valid == 1U);
    assert(snapshot.identity_mismatch == 0U);

    /* Decode the full extended revision tuple, not just architecture/impl. */
    registers[0xA00U / sizeof(uint32_t)] = 0x1732A500U;
    assert(nvx_probe_mapped_bar0(&gpu, registers, sizeof(registers), &snapshot)
           == NVX_PROBE_OK);
    assert(snapshot.extended_architecture == 0x17U);
    assert(snapshot.extended_chip_id == 0x173U);
    assert(snapshot.extended_implementation == 3U);
    assert(snapshot.extended_major_revision == 2U);
    assert(snapshot.extended_minor_revision == 10U);
    assert(snapshot.extended_minor_revision_extra == 5U);

    /* BOOT_42 is optional when the caller has mapped only the first 8 bytes. */
    assert(nvx_probe_mapped_bar0(&gpu, registers, 8U, &snapshot) == NVX_PROBE_OK);
    assert(snapshot.pmc_boot_42 == 0U);
    assert(snapshot.extended_revision_valid == 0U);
    assert(snapshot.extended_chip_id == 0U);

    /* A disagreeing extended architecture is reported, not silently trusted. */
    registers[0xA00U / sizeof(uint32_t)] = 0x19000000U;
    assert(nvx_probe_mapped_bar0(&gpu, registers, sizeof(registers), &snapshot)
           == NVX_PROBE_OK);
    assert(snapshot.identity_mismatch == 1U);
    assert(snapshot.identified_family == NVX_FAMILY_AMPERE);

    registers[0xA00U / sizeof(uint32_t)] = 0xFFFFFFFFU;
    registers[0] = 0xFFFFFFFFU;
    registers[1] = 0xFFFFFFFFU;
    assert(nvx_probe_mapped_bar0(&gpu, registers, sizeof(registers), &snapshot)
           == NVX_PROBE_UNRESPONSIVE_REGISTERS);
    registers[0] = 0U;
    registers[1] = 0U;
    assert(nvx_probe_mapped_bar0(&gpu, registers, sizeof(registers), &snapshot)
           == NVX_PROBE_UNRESPONSIVE_REGISTERS);

    registers[0] = 0x17000000U;
    registers[1] = 1U;
    assert(nvx_probe_mapped_bar0(&gpu, registers, 4U, &snapshot)
           == NVX_PROBE_BAR_TOO_SMALL);
    assert(nvx_probe_mapped_bar0(&gpu,
           (volatile uint32_t *)(void *)((uintptr_t)registers + 1U),
           sizeof(registers), &snapshot) == NVX_PROBE_BAD_MAPPING_ALIGNMENT);
    assert(snapshot.pmc_boot_0 == 0U);
    assert(nvx_probe_mapped_bar0(&other, registers, sizeof(registers), &snapshot)
           == NVX_PROBE_NOT_NVIDIA_DISPLAY);
    mock_command = 0U;
    assert(nvx_probe_mapped_bar0(&gpu, registers, sizeof(registers), &snapshot)
           == NVX_PROBE_MEMORY_DECODE_DISABLED);
    mock_command = NVX_PCI_COMMAND_MEMORY_SPACE;
    assert(nvx_probe_mapped_bar0(&gpu, registers, sizeof(registers), 0)
           == NVX_PROBE_INVALID_ARGUMENT);
    return 0;
}
