/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_attach.h"

#include <assert.h>
#include <stdint.h>

static uint16_t mock_command = NVX_PCI_COMMAND_MEMORY_SPACE;
static unsigned map_calls;
static unsigned unmap_calls;
static int map_failure;
static volatile uint32_t fake_registers[0x1000U / sizeof(uint32_t)] = { 0x17000000U, 1U };

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

int pci_get_bar(const struct pci_device *device, uint8_t index,
                uint64_t *address, int *is_io, int *is_64bit)
{
    (void)device; (void)index; (void)address; (void)is_io; (void)is_64bit;
    return 0;
}

int pci_get_bar_size(const struct pci_device *device, uint8_t index,
                     uint64_t *size)
{
    (void)device; (void)index;
    if (size != 0)
        *size = 0;
    return 0;
}

void *mmio_map_region(uint64_t physical_address, size_t length)
{
    ++map_calls;
    assert(physical_address == 0x10000000ULL);
    assert(length == 0x1000U);
    return map_failure ? 0 : (void *)(uintptr_t)fake_registers;
}

int mmio_unmap_region(void *virtual_address, size_t length)
{
    assert(virtual_address == (void *)(uintptr_t)fake_registers);
    assert(length == 0x1000U);
    ++unmap_calls;
    return 1;
}

int main(void)
{
    const struct pci_device gpu = {
        .vendor_id = NVX_PCI_VENDOR_NVIDIA,
        .device_id = 0x2503U,
        .class_code = 0x03U
    };
    const struct pci_device other = {
        .vendor_id = 0x1234U, .device_id = 0x1111U, .class_code = 0x03U
    };
    const struct pci_device unsupported_nvidia = {
        .vendor_id = NVX_PCI_VENDOR_NVIDIA, .device_id = 0x1234U,
        .class_code = 0x03U
    };
    const nvx_bar_resource_t bar0 = {
        .base = 0x10000000ULL, .size = 0x1000ULL,
        .is_io = 0, .is_64bit = 1, .size_known = 1
    };
    nvx_attached_device_t device = {0};
    nvx_bar_resource_t unknown = bar0;

    assert(nvx_device_attach(&gpu, &bar0, &device) == NVX_ATTACH_OK);
    assert(device.attached == 1U);
    assert(device.bar0_mapping == (void *)(uintptr_t)fake_registers);
    assert(device.lifecycle.state == NVX_STATE_PROBED);
    assert(device.lifecycle.verified_capabilities == 0U);
    assert(device.registers.identified_profile == NVX_PROFILE_GEFORCE_RTX_3060);
    assert(map_calls == 1U && unmap_calls == 0U);
    assert(nvx_device_attach(&gpu, &bar0, &device) == NVX_ATTACH_ALREADY_ATTACHED);
    assert(nvx_device_detach(&device) == NVX_ATTACH_OK);
    assert(device.attached == 0U && device.bar0_mapping == 0);
    assert(device.lifecycle.state == NVX_STATE_STOPPED);
    assert(device.registers.pmc_boot_0 == 0U);
    assert(device.registers.pmc_boot_42 == 0U);
    assert(device.registers.extended_chip_id == 0U);
    assert(device.registers.extended_revision_valid == 0U);
    assert(device.registers.identity_mismatch == 0U);
    assert(device.bar0.base == 0U && device.bar0.size == 0U);
    assert(unmap_calls == 1U);

    assert(nvx_device_attach(&other, &bar0, &device) == NVX_ATTACH_INVALID_ARGUMENT);
    unsigned maps_before_unsupported = map_calls;
    assert(nvx_device_attach(&unsupported_nvidia, &bar0, &device) ==
           NVX_ATTACH_INVALID_ARGUMENT);
    assert(map_calls == maps_before_unsupported);
    unknown.size_known = 0;
    assert(nvx_device_attach(&gpu, &unknown, &device) == NVX_ATTACH_RESOURCE_ERROR);
    assert(device.lifecycle.state == NVX_STATE_FAILED);

    device = (nvx_attached_device_t){0};
    mock_command = 0;
    assert(nvx_device_attach(&gpu, &bar0, &device) == NVX_ATTACH_MAPPING_ERROR);
    assert(map_calls == 1U);
    mock_command = NVX_PCI_COMMAND_MEMORY_SPACE;

    device = (nvx_attached_device_t){0};
    map_failure = 1;
    assert(nvx_device_attach(&gpu, &bar0, &device) == NVX_ATTACH_MAPPING_ERROR);
    assert(device.bar0_mapping == 0 && device.attached == 0U);
    assert(map_calls == 2U);
    map_failure = 0;

    device = (nvx_attached_device_t){0};
    fake_registers[0] = 0xFFFFFFFFU;
    fake_registers[1] = 0xFFFFFFFFU;
    assert(nvx_device_attach(&gpu, &bar0, &device) == NVX_ATTACH_PROBE_ERROR);
    assert(device.bar0_mapping == 0 && device.attached == 0U);
    assert(device.lifecycle.state == NVX_STATE_FAILED);
    assert(unmap_calls == 2U);
    return 0;
}
