/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_core.h"

#include <stddef.h>
#include <string.h>
#include "pci.h"
#include "intel_gpu.h"
#include "amd_gpu.h"
#include "serial.h"

static int gpu_transition_allowed(gpu_device_state_t current,
                                 gpu_device_state_t next)
{
    switch (current) {
        case GPU_DEVICE_DISCOVERED:
            return next == GPU_DEVICE_PROBING || next == GPU_DEVICE_FAILED;
        case GPU_DEVICE_PROBING:
            return next == GPU_DEVICE_INITIALIZED || next == GPU_DEVICE_FAILED;
        case GPU_DEVICE_INITIALIZED:
            return next == GPU_DEVICE_RUNNING || next == GPU_DEVICE_QUIESCING ||
                   next == GPU_DEVICE_FAILED;
        case GPU_DEVICE_RUNNING:
            return next == GPU_DEVICE_QUIESCING || next == GPU_DEVICE_FAILED;
        case GPU_DEVICE_QUIESCING:
            return next == GPU_DEVICE_INITIALIZED || next == GPU_DEVICE_FAILED;
        case GPU_DEVICE_FAILED:
        default:
            return 0;
    }
}

int gpu_device_construct(gpu_device_t *device,
                         const struct pci_device *pci_device,
                         const gpu_pci_match_t *match)
{
    gpu_pci_match_t verified_match;

    if (device == NULL || pci_device == NULL || match == NULL ||
        !gpu_classify_pci_function(pci_device, &verified_match) ||
        !match->is_display_function ||
        match->vendor != verified_match.vendor ||
        match->architecture != verified_match.architecture ||
        match->vendor_id != verified_match.vendor_id ||
        match->device_id != verified_match.device_id ||
        match->is_known_device_id != verified_match.is_known_device_id)
        return 0;

    memset(device, 0, sizeof(*device));
    device->identity.vendor_id = pci_device->vendor_id;
    device->identity.device_id = pci_device->device_id;
    device->identity.revision_id = pci_device->revision;
    device->identity.class_code = pci_device->class_code;
    device->identity.subclass = pci_device->subclass;
    device->identity.programming_interface = pci_device->prog_if;
    device->vendor = match->vendor;
    device->architecture = match->architecture;
    device->state = GPU_DEVICE_DISCOVERED;
    device->pci_device = pci_device;
    return 1;
}

int gpu_device_transition(gpu_device_t *device, gpu_device_state_t next_state)
{
    if (device == NULL || !gpu_transition_allowed(device->state, next_state))
        return 0;

    if (next_state != GPU_DEVICE_RUNNING)
        device->verified_capabilities = 0;
    device->state = next_state;
    return 1;
}

int gpu_device_publish_capabilities(gpu_device_t *device,
                                    uint64_t capabilities)
{
    if (device == NULL || device->state != GPU_DEVICE_RUNNING ||
        (capabilities & ~GPU_CAPABILITY_MASK) != 0)
        return 0;

    device->verified_capabilities = capabilities;
    return 1;
}

void gpu_device_clear_capabilities(gpu_device_t *device)
{
    if (device != NULL)
        device->verified_capabilities = 0;
}

int gpu_driver_matches(const gpu_driver_descriptor_t *driver,
                       const struct pci_device *device)
{
    if (driver == NULL || device == NULL || driver->name == NULL ||
        driver->match == NULL || driver->ops == NULL ||
        device->vendor_id != driver->vendor_id)
        return 0;
    return driver->match(device) != 0;
}

void gpu_initialize(void)
{
    gpu_device_t devices[64];
    size_t count = 0U;

    if (!gpu_discover_pci(devices, sizeof(devices) / sizeof(devices[0]),
                          &count)) {
        serial_write("GPU: PCI display inventory unavailable or exceeds limit\n");
        return;
    }

    for (size_t i = 0U; i < count; ++i) {
        const gpu_device_identity_t *id = &devices[i].identity;
        static const char digits[] = "0123456789ABCDEF";
        char hex[4];

        for (size_t j = 0U; j < sizeof(hex); ++j) {
            unsigned shift = (unsigned)((sizeof(hex) - 1U - j) * 4U);
            hex[j] = digits[(id->device_id >> shift) & 0xFU];
        }
        serial_write("GPU: discovered ");
        serial_write(gpu_vendor_name(devices[i].vendor));
        serial_write(" device 0x");
        serial_write_bytes(hex, sizeof(hex));
        serial_write(" architecture=");
        serial_write(gpu_architecture_name(devices[i].architecture));
        serial_write("\n");

        /* No MMIO, reset, firmware, or DMA until an ASIC-specific backend is verified. */
        if (devices[i].vendor == GPU_VENDOR_INTEL) {
            intel_gpu_descriptor_t descriptor;
            if (!intel_gpu_describe(devices[i].pci_device, &descriptor))
                serial_write("GPU: Intel resource inventory rejected\n");
            else if (!descriptor.mmio_aperture_enabled)
                serial_write("GPU: Intel MMIO aperture not enabled/assigned\n");
        } else if (devices[i].vendor == GPU_VENDOR_AMD) {
            amd_gpu_descriptor_t descriptor;
            if (!amd_gpu_describe(devices[i].pci_device, &descriptor))
                serial_write("GPU: AMD resource inventory rejected\n");
            else if (!descriptor.mmio_aperture_enabled)
                serial_write("GPU: AMD MMIO aperture not enabled/assigned\n");
        }
    }
}

int gpu_discover_pci(gpu_device_t *out_devices, size_t capacity,
                     size_t *out_count)
{
    size_t required = 0;
    int inventory_count;

    if (out_count == NULL || (capacity != 0U && out_devices == NULL))
        return 0;
    *out_count = 0U;

    inventory_count = pci_get_device_count();
    if (inventory_count < 0)
        return 0;

    for (int i = 0; i < inventory_count; ++i) {
        const struct pci_device *pci = pci_get_device(i);
        gpu_pci_match_t match;
        if (pci != NULL && gpu_classify_pci_function(pci, &match))
            ++required;
    }

    if (required > capacity)
        return 0;

    size_t written = 0U;
    for (int i = 0; i < inventory_count; ++i) {
        const struct pci_device *pci = pci_get_device(i);
        gpu_pci_match_t match;
        if (pci == NULL || !gpu_classify_pci_function(pci, &match))
            continue;
        if (!gpu_device_construct(&out_devices[written], pci, &match))
            return 0;
        ++written;
    }

    *out_count = written;
    return 1;
}
