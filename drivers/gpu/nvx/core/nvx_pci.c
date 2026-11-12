/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "nvx_device.h"
#include "pci.h"
#include "serial.h"
#include "mmio.h"
#include "nvx_generation.h"
#include "nvx_probe.h"

#include <stdint.h>

/*
 * PCI discovery and a bounded, read-only PMC identity probe for the three
 * audited profiles. This path never changes PCI command bits, bus mastering,
 * Falcon state, firmware, or engine state; it is safe to skip on any failure.
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
        case NVX_FAMILY_MAXWELL: return "MAXWELL";
        case NVX_FAMILY_PASCAL: return "PASCAL";
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
        case NVX_PROFILE_GEFORCE_MX130: return "GEFORCE-MX130";
        case NVX_PROFILE_GEFORCE_MX150: return "GEFORCE-MX150";
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

/*
 * MX130/MX150 use the shared NVX bounded BAR/MMIO probe for a read-only
 * PMC_BOOT_0/1 identity check. Only the first two dwords are mapped; no
 * PCI command bits, bus mastering, firmware, or engine state are changed.
 */
static void nvx_probe_candidate_identity(const struct pci_device *device)
{
    const nvx_generation_descriptor_t *generation =
        nvx_generation_for_pci(device->device_id);
    uint16_t command;
    uint64_t base = 0U;
    int is_io = 0, is_64bit = 0;
    void *mapping;
    nvx_register_snapshot_t snapshot;
    nvx_probe_status_t status;
    int unmapped;
    size_t probe_bytes = 8U;

    if (generation == NULL)
        return;
    if (generation->pmc_boot_42_offset != UINT32_MAX) {
        if ((uint64_t)generation->pmc_boot_42_offset >
            (uint64_t)SIZE_MAX - sizeof(uint32_t)) {
            serial_write("[NVX] PMC identity: skipped (invalid register extent)\\n");
            return;
        }
        probe_bytes = (size_t)generation->pmc_boot_42_offset +
                      sizeof(uint32_t);
    }

    serial_write("[NVX] PMC identity: ");
    command = pci_read_config16(device, 0x04U);
    if (command == 0xFFFFU ||
        (command & NVX_PCI_COMMAND_MEMORY_SPACE) == 0U) {
        serial_write("skipped (memory decode disabled)\n");
        return;
    }
    if (!pci_get_bar(device, 0U, &base, &is_io, &is_64bit) ||
        is_io != 0 || base == 0U || (base & 0x0FU) != 0U ||
        base > UINT64_MAX - (uint64_t)(probe_bytes - 1U)) {
        serial_write("skipped (invalid BAR0)\n");
        return;
    }

    mapping = mmio_map_region(base, probe_bytes);
    if (mapping == NULL) {
        serial_write("skipped (MMIO mapping unavailable)\n");
        return;
    }
    status = nvx_probe_mapped_bar0(device, (volatile uint32_t *)mapping,
                                   probe_bytes, &snapshot);
    unmapped = mmio_unmap_region(mapping, 8U);
    if (!unmapped) {
        serial_write("failed (MMIO unmap failed)\n");
        return;
    }
    if (status != NVX_PROBE_OK || snapshot.identity_mismatch ||
        snapshot.identified_family != generation->family ||
        snapshot.identified_profile != generation->profile) {
        serial_write("failed (PCI/PMC identity did not agree)\n");
        return;
    }

    serial_write("verified family=");
    serial_write(nvx_family_name(snapshot.identified_family));
    serial_write(" profile=");
    serial_write(nvx_profile_name(snapshot.identified_profile));
    serial_write(" architecture=0x");
    nvx_write_hex(snapshot.architecture, 2U);
    serial_write("\n");
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
            ? " identification=UNRECOGNIZED-PCI-ID"
            : " identification=PCI-ID-TABLE-ONLY");
        serial_write(" pci-command=0x");
        nvx_write_hex(descriptor.pci_command, 4);
        serial_write((descriptor.pci_command & NVX_PCI_COMMAND_MEMORY_SPACE) != 0
            ? " memory-space=enabled"
            : " memory-space=disabled");
        serial_write((descriptor.pci_command & NVX_PCI_COMMAND_BUS_MASTER) != 0
            ? " bus-master=enabled\n"
            : " bus-master=disabled\n");
        nvx_log_bar0(device);
        nvx_probe_candidate_identity(device);
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
