/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_INTEL_GPU_H
#define VANTA_INTEL_GPU_H

#include "gpu_core.h"
#include "gpu_pci_resources.h"
#include "intel_gen9.h"
#include "intel_gen9_device_runtime.h"

#define INTEL_GPU_PCI_VENDOR_ID GPU_PCI_VENDOR_INTEL

/*
 * Caller-owned binding for an already constructed Gen9 LRCA/context and ring.
 * The context workload must write expected_fence_value to fence_cpu_address.
 * quiesce_engine must stop all engine access to that context before returning 1.
 * The binding and runtime must remain at stable addresses.
 */
typedef struct intel_gpu_runtime_binding {
    intel_gen9_device_runtime_t runtime;
    uint64_t lrca_ggtt_address;
    uint32_t context_id;
    uint16_t descriptor_flags;
    const volatile uint32_t *fence_cpu_address;
    uint32_t expected_fence_value;
    unsigned completion_timeout_iterations;
    int (*quiesce_engine)(void *context);
    void *quiesce_context;
    uint8_t context_ready;
    uint8_t configured;
    uint8_t initialized;
    uint8_t fence_verified;
} intel_gpu_runtime_binding_t;

typedef struct intel_gpu_descriptor {
    gpu_pci_match_t pci;
    uint8_t firmware_required;
    uint8_t acceleration_ready;
    uint8_t memory_space_enabled;
    uint8_t mmio_aperture_enabled;
    uint16_t pci_command;
    gpu_pci_resource_inventory_t resources;
    intel_gen9_status_t gen9_profile_status;
    intel_gen9_profile_t gen9_profile;
} intel_gpu_descriptor_t;

/* Read-only PCI identity and assigned BAR inventory; no decode/DMA changes. */
int intel_gpu_match(const struct pci_device *device);
int intel_gpu_describe(const struct pci_device *device,
                       intel_gpu_descriptor_t *out_descriptor);
const gpu_driver_descriptor_t *intel_gpu_driver(void);
/* Configure before gpu_driver_start; binding storage must be zero-initialized. */
int intel_gpu_configure_runtime(gpu_device_t *device,
                                intel_gpu_runtime_binding_t *binding,
                                uint64_t lrca_ggtt_address,
                                uint32_t context_id,
                                uint16_t descriptor_flags,
                                const volatile uint32_t *fence_cpu_address,
                                uint32_t expected_fence_value,
                                unsigned completion_timeout_iterations,
                                int context_ready,
                                int (*quiesce_engine)(void *context),
                                void *quiesce_context);

#endif
