/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_attach.h"

#include "nvx_mmio.h"

#include <stdint.h>
#include <string.h>

static void nvx_attach_clear(nvx_attached_device_t *device)
{
    device->pci_device = 0;
    memset(&device->bar0, 0, sizeof(device->bar0));
    device->bar0_mapping = 0;
    memset(&device->registers, 0, sizeof(device->registers));
    device->registers.identified_family = NVX_FAMILY_UNKNOWN;
    device->registers.identified_profile = NVX_PROFILE_UNKNOWN;
    device->attached = 0;
}

nvx_attach_status_t nvx_device_attach(
    const struct pci_device *pci_device,
    const nvx_bar_resource_t *trusted_bar0,
    nvx_attached_device_t *out_device)
{
    nvx_resource_status_t resource_status;
    nvx_probe_status_t probe_status;
    nvx_attach_status_t result;

    if (out_device == 0)
        return NVX_ATTACH_INVALID_ARGUMENT;
    if (out_device->attached || out_device->bar0_mapping != 0)
        return NVX_ATTACH_ALREADY_ATTACHED;

    nvx_attach_clear(out_device);
    nvx_lifecycle_init(&out_device->lifecycle);

    if (pci_device == 0 || trusted_bar0 == 0 ||
        !nvx_match_pci_device(pci_device))
        return NVX_ATTACH_INVALID_ARGUMENT;

    if (!nvx_lifecycle_transition(&out_device->lifecycle, NVX_STATE_PROBING))
        return NVX_ATTACH_LIFECYCLE_ERROR;

    /* The caller must supply trusted resource-manager size metadata. */
    resource_status = nvx_bar_resource_validate(trusted_bar0);
    if (resource_status != NVX_RESOURCE_OK) {
        (void)nvx_lifecycle_fail(&out_device->lifecycle,
                                 NVX_ERROR_RESOURCE_VALIDATION);
        return NVX_ATTACH_RESOURCE_ERROR;
    }

    if (!nvx_lifecycle_transition(&out_device->lifecycle,
                                  NVX_STATE_RESOURCES_READY))
        return NVX_ATTACH_LIFECYCLE_ERROR;

    if ((pci_read_config16(pci_device, 0x04U) &
         NVX_PCI_COMMAND_MEMORY_SPACE) == 0U) {
        (void)nvx_lifecycle_fail(&out_device->lifecycle,
                                 NVX_ERROR_INITIALIZATION);
        return NVX_ATTACH_MAPPING_ERROR;
    }

    resource_status = nvx_bar_mmio_map(trusted_bar0, &out_device->bar0_mapping);
    if (resource_status != NVX_RESOURCE_OK) {
        out_device->bar0_mapping = 0;
        (void)nvx_lifecycle_fail(&out_device->lifecycle,
                                 NVX_ERROR_RESOURCE_VALIDATION);
        return NVX_ATTACH_MAPPING_ERROR;
    }

    probe_status = nvx_probe_mapped_bar0(
        pci_device, (volatile uint32_t *)out_device->bar0_mapping,
        (size_t)trusted_bar0->size, &out_device->registers);
    if (probe_status != NVX_PROBE_OK) {
        nvx_bar_mmio_unmap(trusted_bar0, out_device->bar0_mapping);
        out_device->bar0_mapping = 0;
        (void)nvx_lifecycle_fail(&out_device->lifecycle,
                                 NVX_ERROR_INITIALIZATION);
        out_device->pci_device = 0;
        out_device->bar0 = *trusted_bar0;
        out_device->attached = 0;
        return NVX_ATTACH_PROBE_ERROR;
    }

    /*
     * Do not mark INITIALIZED: this only proves a responsive PMC register
     * aperture. The next stage may begin GSP/engine initialization; memory
     * management, display and acceleration still need separate verification.
     */
    if (!nvx_lifecycle_transition(&out_device->lifecycle, NVX_STATE_PROBED)) {
        nvx_bar_mmio_unmap(trusted_bar0, out_device->bar0_mapping);
        nvx_attach_clear(out_device);
        (void)nvx_lifecycle_fail(&out_device->lifecycle,
                                 NVX_ERROR_INITIALIZATION);
        return NVX_ATTACH_LIFECYCLE_ERROR;
    }
    out_device->pci_device = pci_device;
    out_device->bar0 = *trusted_bar0;
    out_device->attached = 1;
    result = NVX_ATTACH_OK;
    return result;
}

nvx_attach_status_t nvx_device_detach(nvx_attached_device_t *device)
{
    if (device == 0)
        return NVX_ATTACH_INVALID_ARGUMENT;
    if (!device->attached || device->bar0_mapping == 0)
        return NVX_ATTACH_INVALID_ARGUMENT;

    if (!nvx_lifecycle_transition(&device->lifecycle, NVX_STATE_QUIESCING))
        return NVX_ATTACH_LIFECYCLE_ERROR;

    nvx_bar_mmio_unmap(&device->bar0, device->bar0_mapping);
    device->bar0_mapping = 0;
    nvx_attach_clear(device);

    if (!nvx_lifecycle_transition(&device->lifecycle, NVX_STATE_STOPPED))
        return NVX_ATTACH_LIFECYCLE_ERROR;
    return NVX_ATTACH_OK;
}
