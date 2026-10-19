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
 * Phase 1 only: inventory NVIDIA PCI functions. This code deliberately does
 * not map BARs, enable PCI command bits, touch device registers, or initialize
 * any GPU engine. PCI identity is not a claim of hardware support.
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

static void nvx_log_identity(const struct pci_device *device)
{
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
    uint8_t header_type = pci_read_config8(device, 0x0E) & 0x7FU;
    if (header_type == 0)
    {
        uint16_t subsystem_vendor = pci_read_config16(device, 0x2C);
        uint16_t subsystem_device = pci_read_config16(device, 0x2E);
        serial_write(" subsystem=0x");
        nvx_write_hex(subsystem_vendor, 4);
        serial_write(":0x");
        nvx_write_hex(subsystem_device, 4);
    }

    serial_write("\n");

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
        if (is_64bit)
            serial_write(" width=64");
        else
            serial_write(" width=32");
        serial_write("\n");
    }
    else
    {
        serial_write("[NVX] BAR0 absent or unsupported; no mapping attempted\n");
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
