/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_VM_H
#define VANTA_GPU_VM_H

#include <stddef.h>
#include <stdint.h>
#include "gpu_dma.h"
#include "gpu_va.h"
#include "spinlock.h"

#define GPU_VM_PAGE_SIZE GPU_VA_PAGE_SIZE
#define GPU_VM_MAX_MAPPINGS 128U
#define GPU_VM_ACCESS_READ  1U
#define GPU_VM_ACCESS_WRITE 2U

typedef enum gpu_vm_status {
    GPU_VM_OK = 0,
    GPU_VM_INVALID_ARGUMENT,
    GPU_VM_NOT_INITIALIZED,
    GPU_VM_OUT_OF_SPACE,
    GPU_VM_TABLE_FULL,
    GPU_VM_INVALID_HANDLE,
    GPU_VM_STALE_HANDLE,
    GPU_VM_DMA_INVALID,
    GPU_VM_VA_INVALID,
    GPU_VM_MAP_FAILED,
    GPU_VM_UNMAP_FAILED,
    GPU_VM_FLUSH_FAILED,
    GPU_VM_ROLLBACK_FAILED,
    GPU_VM_BUSY,
    GPU_VM_DEVICE_FAULTED,
    GPU_VM_CORRUPT_STATE
} gpu_vm_status_t;

typedef struct gpu_vm_ops {
    void *context;
    /*
     * A failed map_page must leave that page unchanged. A failed unmap_page
     * must leave that page mapped. Callbacks execute under the VM lock and
     * must not re-enter this manager.
     */
    int (*map_page)(void *context, uint64_t gpu_address,
                    uint64_t physical_address, unsigned access);
    int (*unmap_page)(void *context, uint64_t gpu_address);
    int (*flush)(void *context);
} gpu_vm_ops_t;

typedef struct gpu_vm_handle {
    uint64_t token;
    uint32_t generation;
} gpu_vm_handle_t;

typedef struct gpu_vm_record {
    const gpu_va_allocation_t *va;
    const gpu_dma_buffer_t *dma;
    uint64_t token;
    uint32_t generation;
    size_t mapped_pages;
    uint8_t pending_flush;
    uint8_t active;
} gpu_vm_record_t;

typedef struct gpu_vm {
    spinlock_t lock;
    gpu_va_space_t *va_space;
    gpu_vm_ops_t ops;
    gpu_vm_record_t mappings[GPU_VM_MAX_MAPPINGS];
    uint64_t next_token;
    uint32_t generation;
    size_t mapping_count;
    uint8_t initialized;
    uint8_t faulted;
} gpu_vm_t;

/* Zero-initialize manager before first use. The VA space is externally owned. */
gpu_vm_status_t gpu_vm_init(gpu_vm_t *vm, gpu_va_space_t *va_space,
                            const gpu_vm_ops_t *ops);
/*
 * Maps an existing DMA allocation into an existing VA reservation. The caller
 * owns both objects and must keep them alive until gpu_vm_unmap succeeds.
 */
gpu_vm_status_t gpu_vm_map_dma(gpu_vm_t *vm,
                               const gpu_va_allocation_t *va,
                               const gpu_dma_buffer_t *dma,
                               unsigned access, gpu_vm_handle_t *out_handle);
/* Retries partial rollback/unmap and TLB flush failures using the same handle. */
gpu_vm_status_t gpu_vm_unmap(gpu_vm_t *vm, gpu_vm_handle_t *handle);
gpu_vm_status_t gpu_vm_validate(gpu_vm_t *vm);
gpu_vm_status_t gpu_vm_destroy(gpu_vm_t *vm);

#endif
