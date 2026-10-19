/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_ATTACH_H
#define VANTA_NVX_ATTACH_H

#include "nvx_device.h"
#include "nvx_lifecycle.h"
#include "nvx_probe.h"
#include "nvx_resource.h"

typedef enum nvx_attach_status {
    NVX_ATTACH_OK = 0,
    NVX_ATTACH_INVALID_ARGUMENT,
    NVX_ATTACH_LIFECYCLE_ERROR,
    NVX_ATTACH_RESOURCE_ERROR,
    NVX_ATTACH_MAPPING_ERROR,
    NVX_ATTACH_PROBE_ERROR,
    NVX_ATTACH_ALREADY_ATTACHED
} nvx_attach_status_t;

/*
 * Runtime handle for a mapped, register-responsive candidate. The BAR extent
 * must come from a trusted platform resource allocator; NVX never guesses it.
 * A successful attach means PCI/MMIO probe succeeded, not that 3D acceleration
 * or firmware initialization is available.
 */
typedef struct nvx_attached_device {
    const struct pci_device *pci_device;
    nvx_bar_resource_t bar0;
    void *bar0_mapping;
    nvx_register_snapshot_t registers;
    nvx_lifecycle_t lifecycle;
    uint8_t attached;
} nvx_attached_device_t;

nvx_attach_status_t nvx_device_attach(
    const struct pci_device *pci_device,
    const nvx_bar_resource_t *trusted_bar0,
    nvx_attached_device_t *out_device);

/* Unmaps BAR0 and returns the handle to an empty, non-capable state. */
nvx_attach_status_t nvx_device_detach(nvx_attached_device_t *device);

#endif
