/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_gpu.h"

#include <string.h>
#include "pci.h"

static int amd_gpu_probe(gpu_device_t *device)
{
    return device != 0 && device->vendor == GPU_VENDOR_AMD &&
           device->state == GPU_DEVICE_PROBING;
}

static int amd_gpu_initialize(gpu_device_t *device)
{
    (void)device;
    /* Deliberately fail closed until PSP, GMC and engine setup exist. */
    return 0;
}

static int amd_gpu_quiesce(gpu_device_t *device)
{
    return device != 0 && device->state == GPU_DEVICE_RUNNING;
}

static void amd_gpu_shutdown(gpu_device_t *device)
{
    if (device != 0)
        gpu_device_clear_capabilities(device);
}

static const gpu_driver_ops_t amd_ops = {
    .probe = amd_gpu_probe,
    .initialize = amd_gpu_initialize,
    .quiesce = amd_gpu_quiesce,
    .shutdown = amd_gpu_shutdown
};

static const gpu_driver_descriptor_t amd_driver = {
    .name = "amd-gpu",
    .vendor_id = AMD_GPU_PCI_VENDOR_ID,
    .match = amd_gpu_match,
    .ops = &amd_ops
};

int amd_gpu_match(const struct pci_device *device)
{
    gpu_pci_match_t match;
    return gpu_classify_pci_function(device, &match) &&
           match.vendor == GPU_VENDOR_AMD;
}

int amd_gpu_describe(const struct pci_device *device,
                     amd_gpu_descriptor_t *out_descriptor)
{
    gpu_pci_match_t match;
    uint16_t command;

    if (out_descriptor == 0 ||
        !gpu_classify_pci_function(device, &match) ||
        match.vendor != GPU_VENDOR_AMD)
        return 0;

    memset(out_descriptor, 0, sizeof(*out_descriptor));
    out_descriptor->pci = match;
    if (!gpu_pci_resources_scan(device, &out_descriptor->resources))
        return 0;

    command = pci_read_config16(device, 0x04U);
    out_descriptor->pci_command = command;
    out_descriptor->memory_space_enabled =
        (uint8_t)((command & 0x0002U) != 0U);
    out_descriptor->mmio_aperture_enabled =
        (uint8_t)(out_descriptor->memory_space_enabled &&
                  out_descriptor->resources.primary_mmio_index !=
                      GPU_PCI_BAR_NONE);
    out_descriptor->firmware_required = 1U;
    /* Never claim acceleration before PSP, GMC and graphics queues work. */
    out_descriptor->acceleration_ready = 0U;
    return 1;
}

const gpu_driver_descriptor_t *amd_gpu_driver(void)
{
    return &amd_driver;
}
