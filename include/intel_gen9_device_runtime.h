/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_INTEL_GEN9_DEVICE_RUNTIME_H
#define VANTA_INTEL_GEN9_DEVICE_RUNTIME_H

#include "gpu_pci_mmio.h"
#include "intel_gen9_execlist.h"

#define INTEL_GEN9_DEVICE_FORCEWAKE_TIMEOUT 100000U

typedef enum intel_gen9_device_status {
    INTEL_GEN9_DEVICE_OK = 0,
    INTEL_GEN9_DEVICE_INVALID_ARGUMENT,
    INTEL_GEN9_DEVICE_UNSUPPORTED,
    INTEL_GEN9_DEVICE_NOT_READY,
    INTEL_GEN9_DEVICE_MMIO_FAILED,
    INTEL_GEN9_DEVICE_FORCEWAKE_FAILED,
    INTEL_GEN9_DEVICE_PCI_FAILED,
    INTEL_GEN9_DEVICE_SUBMISSION_FAILED,
    INTEL_GEN9_DEVICE_COMPLETION_TIMEOUT,
    INTEL_GEN9_DEVICE_SHUTDOWN_FAILED
} intel_gen9_device_status_t;

/*
 * This layer owns BAR0 mapping, render forcewake and PCI bus mastering.
 * The caller must provide an already initialized, GPU-visible LRCA/context
 * image and command ring. It does not load GuC firmware or create contexts.
 */
typedef struct intel_gen9_device_runtime {
    const struct pci_device *pci_device;
    gpu_pci_mmio_mapping_t mapping;
    intel_gen9_execlist_t execlist;
    uint32_t context_id;
    uint64_t context_descriptor;
    uint8_t initialized;
    uint8_t forcewake_held;
    uint8_t bus_master_enabled_by_us;
    uint8_t faulted;
} intel_gen9_device_runtime_t;

/* Zero-initialize runtime; device must be quiesced and context_ready verified. */
intel_gen9_device_status_t intel_gen9_device_runtime_init(
    intel_gen9_device_runtime_t *runtime, const struct pci_device *device,
    uint64_t lrca_ggtt_address, uint32_t context_id,
    uint16_t descriptor_flags, int context_ready, int device_quiesced);

/* One in-flight context at a time. */
intel_gen9_device_status_t intel_gen9_device_submit(
    intel_gen9_device_runtime_t *runtime);
intel_gen9_device_status_t intel_gen9_device_wait(
    intel_gen9_device_runtime_t *runtime, unsigned timeout_iterations);

/* Does not discard a pending descriptor or unmap hardware while work is live. */
intel_gen9_device_status_t intel_gen9_device_runtime_shutdown(
    intel_gen9_device_runtime_t *runtime, int device_quiesced);

#endif
