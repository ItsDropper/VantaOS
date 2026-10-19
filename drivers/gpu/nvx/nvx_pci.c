/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "nvx_device.h"
#include "pci.h"
#include "serial.h"

#include <stdint.h>

/*
 * PCI discovery and family identification only. No BAR mapping, PCI command
 * changes, device-register access, firmware loading, or engine initialization.
 */

static void nvx_write_hex(uint32_t value, unsigned digits)
{
    static const char hex[] = "0123456789ABCDEF";
    char buffer[8];
    if (digits > sizeof(buffer))
        digits = sizeof(buffer);

    for (unsigned i = 0; i < digits; ++i)
    {
        unsigned shift = (digits - i - 1U) * 4U;
        buffer[i] = hex[(value >> shift) & 0xFU];
    }
    serial_write_bytes(buffer, digits);
}

static const char *nvx_family_name(nvx_generation_family_t family)
{
    switch (family)
    {
        case NVX_FAMILY_LEGACY: return "LEGACY";
        case NVX_FAMILY_TURING: return "TURING";
        case NVX_FAMILY_AMPERE: return "AMPERE";
        case NVX_FAMILY_ADA: return "ADA";
        case NVX_FAMILY_HOPPER: return "HOPPER";
        case NVX_FAMILY_UNKNOWN:
        default: return "UNKNOWN";
    }
}

static const char *nvx_profile_name(nvx_device_profile_t profile)
{
    switch (profile)
    {
        case NVX_PROFILE_GEFORCE_RTX_3060: return "GEFORCE-RTX-3060";
        case NVX_PROFILE_UNKNOWN:
        default: return "UNKNOWN";
    }
}

static void nvx_log_bar0(const struct pci_device *device)
{
    /*
     * Report BAR0 only as raw discovery metadata. pci_get_bar() does not
     * determine resource size, so no length or mapping is claimed here.
     */
    uint64_t bar_address = 0;
    int is_io = 0;
    int is_64bit = 0;
    if (pci_get_bar(device, 0, &bar_address, &is_io, &is_64bit))
    {
        serial_write("[NVX] BAR0 address=0x");
        nvx_write_hex((uint32_t)(bar_address >> 32), 8);
        nvx_write_hex((uint32_t)bar_address, 8);
        serial_write(is_io ? " type=I/O" : " type=memory");
        serial_write(is_64bit ? " width=64\n" : " width=32\n");
    }
    else
    {
        serial_write("[NVX] BAR0 absent or unsupported; no mapping attempted\n");
    }
}

static void nvx_log_identity(const struct pci_device *device)
{
    nvx_device_descriptor_t descriptor;
    int gpu_candidate = nvx_describe_pci_device(device, &descriptor);

    serial_write("[NVX] NVIDIA PCI function detected: ");
    nvx_write_hex(device->bus, 2);
    serial_write(":");
    nvx_write_hex(device->slot, 2);
    serial_write(".");
    nvx_write_hex(device->function, 1);
    serial_write(" vendor=0x");
    nvx_write_hex(device->vendor_id, 4);
    serial_write(" device=0x");
    nvx_write_hex(device->device_id, 4);
    serial_write(" revision=0x");
    nvx_write_hex(device->revision, 2);
    serial_write(" class=0x");
    nvx_write_hex(device->class_code, 2);
    serial_write(":");
    nvx_write_hex(device->subclass, 2);
    serial_write(" prog-if=0x");
    nvx_write_hex(device->prog_if, 2);

    /*
     * Subsystem ID registers are defined for type-0 PCI headers. Avoid
     * interpreting bridge-header registers as subsystem identity.
     */
    if ((pci_read_config8(device, 0x0EU) & 0x7FU) == 0U)
    {
        uint16_t subsystem_vendor = pci_read_config16(device, 0x2CU);
        uint16_t subsystem_device = pci_read_config16(device, 0x2EU);
        serial_write(" subsystem=0x");
        nvx_write_hex(subsystem_vendor, 4);
        serial_write(":0x");
        nvx_write_hex(subsystem_device, 4);
    }

    if (gpu_candidate)
    {
        serial_write(" role=GPU-candidate family=");
        serial_write(nvx_family_name(descriptor.family));
        serial_write(" profile=");
        serial_write(nvx_profile_name(descriptor.profile));
        serial_write(descriptor.profile == NVX_PROFILE_UNKNOWN
            ? " identification=UNRECOGNIZED-PCI-ID\n"
            : " identification=PCI-ID-TABLE-ONLY\n");
        nvx_log_bar0(device);
    }
    else
    {
        serial_write(" role=non-GPU-NVIDIA-function\n");
    }

    serial_write("[NVX] state=DETECTED; initialization=NOT_ATTEMPTED; acceleration=UNAVAILABLE\n");
}

int nvx_pci_discover(void)
{
    int detected = 0;
    int count = pci_get_device_count();

    if (count < 0)
    {
        serial_write("[NVX] PCI inventory unavailable; discovery skipped\n");
        return 0;
    }

    for (int i = 0; i < count; ++i)
    {
        const struct pci_device *device = pci_get_device(i);
        if (device == 0 || device->vendor_id != NVX_PCI_VENDOR_NVIDIA)
            continue;

        ++detected;
        nvx_log_identity(device);
    }

    if (detected == 0)
        serial_write("[NVX] no NVIDIA PCI functions detected\n");
    else
        serial_write("[NVX] read-only discovery complete; no NVIDIA device initialized\n");

    return detected;
}
