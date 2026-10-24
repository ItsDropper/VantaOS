/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_INTEL_GEN9_MEMORY_H
#define VANTA_INTEL_GEN9_MEMORY_H

#include "gpu_dma.h"
#include "gpu_pci_mmio.h"
#include "intel_gen9_ggtt.h"

#define INTEL_GEN9_MEMORY_GGTT_LIMIT (1ULL << 32U)
#define INTEL_GEN9_MEMORY_DEFAULT_GGTT_BASE 0x10000000ULL
#define INTEL_GEN9_MEMORY_FORCEWAKE_TIMEOUT 100000U
#define INTEL_GEN9_MEMORY_MAX_ALLOCATIONS 64U
#define INTEL_GEN9_MEMORY_VA_SIZE 0x10000000ULL

typedef enum intel_gen9_memory_status {
    INTEL_GEN9_MEMORY_OK = 0,
    INTEL_GEN9_MEMORY_INVALID_ARGUMENT,
    INTEL_GEN9_MEMORY_UNSUPPORTED_DEVICE,
    INTEL_GEN9_MEMORY_NOT_READY,
    INTEL_GEN9_MEMORY_MMIO_FAILED,
    INTEL_GEN9_MEMORY_FORCEWAKE_FAILED,
    INTEL_GEN9_MEMORY_DMA_FAILED,
    INTEL_GEN9_MEMORY_GGTT_FAILED,
    INTEL_GEN9_MEMORY_ADDRESS_BUSY,
    INTEL_GEN9_MEMORY_SHUTDOWN_FAILED
} intel_gen9_memory_status_t;

struct intel_gen9_gpu_allocation;
typedef struct intel_gen9_memory_slot {
    struct intel_gen9_gpu_allocation *allocation;
    uint64_t address;
    size_t size;
} intel_gen9_memory_slot_t;

typedef struct intel_gen9_memory_runtime {
    const struct pci_device *device;
    gpu_pci_mmio_mapping_t mapping;
    intel_gen9_ggtt_forcewake_ops_t forcewake;
    intel_gen9_memory_slot_t slots[INTEL_GEN9_MEMORY_MAX_ALLOCATIONS];
    uint8_t initialized;
    uint8_t faulted;
} intel_gen9_memory_runtime_t;

typedef struct intel_gen9_gpu_allocation {
    intel_gen9_memory_runtime_t *runtime;
    gpu_dma_buffer_t dma;
    uint64_t ggtt_address;
    uint8_t mapped;
} intel_gen9_gpu_allocation_t;

/*
 * Maps the Gen9 GTTMMADR aperture and establishes render forcewake.
 * This enables GPU-visible allocations but does not imply a running render
 * engine, firmware, or 3D acceleration. Caller serializes all GGTT mutations.
 */
intel_gen9_memory_status_t intel_gen9_memory_init(
    intel_gen9_memory_runtime_t *runtime, const struct pci_device *device,
    int device_quiesced);

/*
 * Allocate contiguous DMA and map it into the 32-bit GGTT. Pass address 0 to
 * choose a free page-aligned VA in the driver's reserved 256 MiB window.
 * The runtime and allocation object must remain at stable addresses.
 */
intel_gen9_memory_status_t intel_gen9_memory_alloc(
    intel_gen9_memory_runtime_t *runtime, size_t size, uint64_t ggtt_address,
    intel_gen9_gpu_allocation_t *out_allocation);

/* Unmaps and invalidates before returning the underlying DMA pages. */
intel_gen9_memory_status_t intel_gen9_memory_free(
    intel_gen9_gpu_allocation_t *allocation);

/* Caller must ensure the GPU cannot access any allocations at shutdown. */
intel_gen9_memory_status_t intel_gen9_memory_shutdown(
    intel_gen9_memory_runtime_t *runtime, int device_quiesced);

#endif
