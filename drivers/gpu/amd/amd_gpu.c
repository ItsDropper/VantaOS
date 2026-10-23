/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_gpu.h"
#include "amd_polaris.h"

#include <string.h>
#include "pci.h"

static int amd_gpu_probe(gpu_device_t *device)
{
    return device != 0 && device->vendor == GPU_VENDOR_AMD &&
           device->state == GPU_DEVICE_PROBING &&
           amd_polaris_match_id(device->identity.vendor_id,
                                device->identity.device_id);
}

static int amd_gpu_initialize(gpu_device_t *device)
{
    amd_gpu_runtime_binding_t *binding;
    amd_polaris_device_status_t status;
    uint32_t fence = 0U;

    if (device == NULL || device->driver_private == NULL ||
        device->pci_device == NULL)
        return 0;
    binding = (amd_gpu_runtime_binding_t *)device->driver_private;
    if (binding->configured == 0U || binding->firmware_ready == 0U ||
        binding->fence_timeout_iterations == 0U)
        return 0;

    status = amd_polaris_device_runtime_init(&binding->runtime,
        device->pci_device, binding->ring_bytes, 1, 1);
    if (status != AMD_POLARIS_DEVICE_OK) {
        binding->initialized = (uint8_t)(binding->runtime.initialized != 0U);
        return 0;
    }
    binding->initialized = 1U;

    /*
     * Do not report a started driver based on register readback alone. Execute
     * the restricted PM4 fence packet and require the GPU to write it back.
     */
    status = amd_polaris_device_submit_fence(&binding->runtime, &fence);
    if (status == AMD_POLARIS_DEVICE_OK)
        status = amd_polaris_device_wait_fence(&binding->runtime, fence,
            binding->fence_timeout_iterations);
    if (status != AMD_POLARIS_DEVICE_OK) {
        if (amd_polaris_device_runtime_shutdown(&binding->runtime, 1) ==
            AMD_POLARIS_DEVICE_OK)
            binding->initialized = 0U;
        return 0;
    }

    binding->fence_verified = 1U;
    return 1;
}

static int amd_gpu_quiesce(gpu_device_t *device)
{
    amd_gpu_runtime_binding_t *binding;

    if (device == NULL || device->state != GPU_DEVICE_QUIESCING)
        return 0;
    if (device->driver_private == NULL)
        return 0;
    binding = (amd_gpu_runtime_binding_t *)device->driver_private;
    if (binding->initialized == 0U)
        return 1;
    if (amd_polaris_device_runtime_shutdown(&binding->runtime, 1) !=
        AMD_POLARIS_DEVICE_OK)
        return 0;
    binding->initialized = 0U;
    binding->fence_verified = 0U;
    return 1;
}

static void amd_gpu_shutdown(gpu_device_t *device)
{
    if (device == NULL)
        return;
    if (device->driver_private != NULL) {
        amd_gpu_runtime_binding_t *binding =
            (amd_gpu_runtime_binding_t *)device->driver_private;
        if (binding->initialized != 0U &&
            amd_polaris_device_runtime_shutdown(&binding->runtime, 1) ==
                AMD_POLARIS_DEVICE_OK) {
            binding->initialized = 0U;
            binding->fence_verified = 0U;
        }
    }
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
    out_descriptor->polaris_profile_status = amd_polaris_inspect(
        device->vendor_id, device->device_id, &out_descriptor->resources,
        &out_descriptor->polaris_profile);

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

int amd_gpu_configure_runtime(gpu_device_t *device,
                              amd_gpu_runtime_binding_t *binding,
                              size_t ring_bytes,
                              unsigned fence_timeout_iterations,
                              int authenticated_firmware_ready)
{
    if (device == NULL || binding == NULL ||
        device->state != GPU_DEVICE_DISCOVERED ||
        device->pci_device == NULL ||
        !amd_polaris_match_id(device->identity.vendor_id,
                              device->identity.device_id) ||
        binding->configured != 0U || binding->initialized != 0U ||
        binding->runtime.initialized != 0U ||
        fence_timeout_iterations == 0U ||
        !authenticated_firmware_ready)
        return 0;

    memset(binding, 0, sizeof(*binding));
    binding->ring_bytes = ring_bytes;
    binding->fence_timeout_iterations = fence_timeout_iterations;
    binding->firmware_ready = 1U;
    binding->configured = 1U;
    device->driver_private = binding;
    return 1;
}
