/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_AMD_POLARIS_MEMORY_H
#define VANTA_AMD_POLARIS_MEMORY_H

#include "gpu_dma.h"
#include "gpu_vm.h"

#define AMD_POLARIS_MEMORY_MAX_ALLOCATIONS 64U
#define AMD_POLARIS_MEMORY_VA_BASE 0x100000000ULL
#define AMD_POLARIS_MEMORY_VA_SIZE 0x100000000ULL

typedef enum amd_polaris_memory_status {
    AMD_POLARIS_MEMORY_OK = 0,
    AMD_POLARIS_MEMORY_INVALID_ARGUMENT,
    AMD_POLARIS_MEMORY_NOT_READY,
    AMD_POLARIS_MEMORY_DMA_FAILED,
    AMD_POLARIS_MEMORY_VA_BUSY,
    AMD_POLARIS_MEMORY_MAP_FAILED,
    AMD_POLARIS_MEMORY_UNMAP_FAILED,
    AMD_POLARIS_MEMORY_FLUSH_FAILED,
    AMD_POLARIS_MEMORY_ROLLBACK_FAILED
} amd_polaris_memory_status_t;

struct amd_polaris_gpu_allocation;
typedef struct amd_polaris_memory_slot {
    struct amd_polaris_gpu_allocation *allocation;
    uint64_t address;
    size_t size;
} amd_polaris_memory_slot_t;

typedef struct amd_polaris_memory_runtime {
    gpu_vm_ops_t vm_ops;
    amd_polaris_memory_slot_t slots[AMD_POLARIS_MEMORY_MAX_ALLOCATIONS];
    uint8_t initialized;
    uint8_t faulted;
} amd_polaris_memory_runtime_t;

typedef struct amd_polaris_gpu_allocation {
    amd_polaris_memory_runtime_t *runtime;
    gpu_dma_buffer_t dma;
    uint64_t gpu_address;
    uint8_t *mapped_pages;
    size_t page_count;
    uint8_t mapped;
} amd_polaris_gpu_allocation_t;

/*
 * Use only with an initialized Polaris VMID page table. The VM ops and
 * allocation handles must remain stable; callers serialize alloc/free.
 */
amd_polaris_memory_status_t amd_polaris_memory_init(
    amd_polaris_memory_runtime_t *runtime, const gpu_vm_ops_t *vm_ops);

/* Pass gpu_address=0 for a free VA in the reserved 4 GiB window. */
amd_polaris_memory_status_t amd_polaris_memory_alloc(
    amd_polaris_memory_runtime_t *runtime, size_t size, uint64_t gpu_address,
    amd_polaris_gpu_allocation_t *out_allocation);

/* GPU mapping is removed and TLB invalidated before DMA is released. */
amd_polaris_memory_status_t amd_polaris_memory_free(
    amd_polaris_gpu_allocation_t *allocation);

/* Device must be quiesced and all allocations freed before shutdown. */
amd_polaris_memory_status_t amd_polaris_memory_shutdown(
    amd_polaris_memory_runtime_t *runtime, int device_quiesced);

#endif
