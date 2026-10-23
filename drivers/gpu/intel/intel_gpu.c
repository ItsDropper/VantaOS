/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gpu.h"
#include "intel_gen9.h"

#include <string.h>
#include "pci.h"

static int intel_gpu_probe(gpu_device_t *device)
{
    return device != 0 && device->vendor == GPU_VENDOR_INTEL &&
           device->state == GPU_DEVICE_PROBING &&
           intel_gen9_match_id(device->identity.vendor_id,
                               device->identity.device_id);
}

static int intel_gpu_initialize(gpu_device_t *device)
{
    intel_gpu_runtime_binding_t *binding;
    intel_gen9_device_status_t status;

    if (device == NULL || device->pci_device == NULL ||
        device->driver_private == NULL)
        return 0;
    binding = (intel_gpu_runtime_binding_t *)device->driver_private;
    if (binding->configured == 0U || binding->context_ready == 0U ||
        binding->completion_timeout_iterations == 0U ||
        binding->quiesce_engine == NULL)
        return 0;

    status = intel_gen9_device_runtime_init(&binding->runtime,
        device->pci_device, binding->lrca_ggtt_address,
        binding->context_id, binding->descriptor_flags,
        binding->context_ready, 1);
    if (status != INTEL_GEN9_DEVICE_OK) {
        binding->initialized = (uint8_t)(binding->runtime.initialized != 0U);
        return 0;
    }
    binding->initialized = 1U;

    /*
     * The caller-prepared context must contain the bounded self-test workload.
     * A register setup success is not enough: require a matching CSB completion.
     */
    status = intel_gen9_device_submit(&binding->runtime);
    if (status == INTEL_GEN9_DEVICE_OK)
        status = intel_gen9_device_wait(&binding->runtime,
            binding->completion_timeout_iterations);
    if (status != INTEL_GEN9_DEVICE_OK) {
        if (binding->quiesce_engine(binding->quiesce_context) &&
            intel_gen9_device_runtime_shutdown(&binding->runtime, 1) ==
                INTEL_GEN9_DEVICE_OK)
            binding->initialized = 0U;
        return 0;
    }

    binding->fence_verified = 1U;
    return 1;
}

static int intel_gpu_quiesce(gpu_device_t *device)
{
    intel_gpu_runtime_binding_t *binding;

    if (device == NULL || device->state != GPU_DEVICE_QUIESCING ||
        device->driver_private == NULL)
        return 0;
    binding = (intel_gpu_runtime_binding_t *)device->driver_private;
    if (binding->initialized == 0U)
        return 1;
    if (binding->quiesce_engine == NULL ||
        !binding->quiesce_engine(binding->quiesce_context))
        return 0;
    if (intel_gen9_device_runtime_shutdown(&binding->runtime, 1) !=
        INTEL_GEN9_DEVICE_OK)
        return 0;
    binding->initialized = 0U;
    binding->fence_verified = 0U;
    return 1;
}

static void intel_gpu_shutdown(gpu_device_t *device)
{
    if (device == NULL)
        return;
    if (device->driver_private != NULL) {
        intel_gpu_runtime_binding_t *binding =
            (intel_gpu_runtime_binding_t *)device->driver_private;
        if (binding->initialized != 0U && binding->quiesce_engine != NULL &&
            binding->quiesce_engine(binding->quiesce_context) &&
            intel_gen9_device_runtime_shutdown(&binding->runtime, 1) ==
                INTEL_GEN9_DEVICE_OK) {
            binding->initialized = 0U;
            binding->fence_verified = 0U;
        }
    }
    gpu_device_clear_capabilities(device);
}

static const gpu_driver_ops_t intel_ops = {
    .probe = intel_gpu_probe,
    .initialize = intel_gpu_initialize,
    .quiesce = intel_gpu_quiesce,
    .shutdown = intel_gpu_shutdown
};

static const gpu_driver_descriptor_t intel_driver = {
    .name = "intel-gpu",
    .vendor_id = INTEL_GPU_PCI_VENDOR_ID,
    .match = intel_gpu_match,
    .ops = &intel_ops
};

int intel_gpu_match(const struct pci_device *device)
{
    gpu_pci_match_t match;
    return gpu_classify_pci_function(device, &match) &&
           match.vendor == GPU_VENDOR_INTEL;
}

int intel_gpu_describe(const struct pci_device *device,
                       intel_gpu_descriptor_t *out_descriptor)
{
    gpu_pci_match_t match;
    uint16_t command;

    if (out_descriptor == 0 ||
        !gpu_classify_pci_function(device, &match) ||
        match.vendor != GPU_VENDOR_INTEL)
        return 0;

    memset(out_descriptor, 0, sizeof(*out_descriptor));
    out_descriptor->pci = match;
    if (!gpu_pci_resources_scan(device, &out_descriptor->resources))
        return 0;
    out_descriptor->gen9_profile_status = intel_gen9_inspect(
        device->vendor_id, device->device_id, &out_descriptor->resources,
        &out_descriptor->gen9_profile);

    command = pci_read_config16(device, 0x04U);
    out_descriptor->pci_command = command;
    out_descriptor->memory_space_enabled =
        (uint8_t)((command & 0x0002U) != 0U);
    out_descriptor->mmio_aperture_enabled =
        (uint8_t)(out_descriptor->memory_space_enabled &&
                  out_descriptor->resources.primary_mmio_index !=
                      GPU_PCI_BAR_NONE);
    out_descriptor->firmware_required = 1U;
    /* Never claim acceleration until GuC/GGTT/engine bring-up is verified. */
    out_descriptor->acceleration_ready = 0U;
    return 1;
}

const gpu_driver_descriptor_t *intel_gpu_driver(void)
{
    return &intel_driver;
}

int intel_gpu_configure_runtime(gpu_device_t *device,
                                intel_gpu_runtime_binding_t *binding,
                                uint64_t lrca_ggtt_address,
                                uint32_t context_id,
                                uint16_t descriptor_flags,
                                unsigned completion_timeout_iterations,
                                int context_ready,
                                int (*quiesce_engine)(void *context),
                                void *quiesce_context)
{
    uint64_t descriptor = 0U;

    if (device == NULL || binding == NULL ||
        device->state != GPU_DEVICE_DISCOVERED ||
        device->pci_device == NULL || device->driver_private != NULL ||
        !intel_gen9_match_id(device->identity.vendor_id,
                             device->identity.device_id) ||
        binding->configured != 0U || binding->initialized != 0U ||
        binding->runtime.initialized != 0U ||
        !context_ready || completion_timeout_iterations == 0U ||
        quiesce_engine == NULL ||
        intel_gen9_execlist_build_descriptor(lrca_ggtt_address, context_id,
            descriptor_flags, &descriptor) != INTEL_GEN9_EXECLIST_OK)
        return 0;

    memset(binding, 0, sizeof(*binding));
    binding->lrca_ggtt_address = lrca_ggtt_address;
    binding->context_id = context_id;
    binding->descriptor_flags = descriptor_flags;
    binding->completion_timeout_iterations = completion_timeout_iterations;
    binding->quiesce_engine = quiesce_engine;
    binding->quiesce_context = quiesce_context;
    binding->context_ready = 1U;
    binding->configured = 1U;
    device->driver_private = binding;
    return 1;
}
