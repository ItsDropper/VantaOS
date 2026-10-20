/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_core.h"

#include <stddef.h>
#include <string.h>

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
    if (device == NULL || pci_device == NULL || match == NULL ||
        !match->is_display_function ||
        match->vendor_id != pci_device->vendor_id ||
        match->device_id != pci_device->device_id)
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
