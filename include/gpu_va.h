/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_VA_H
#define VANTA_GPU_VA_H

#include <stddef.h>
#include <stdint.h>
#include "spinlock.h"

/*
 * Software GPU virtual-address reservation allocator.
 *
 * This manages address-space ownership only. It does not construct vendor-specific
 * page tables, map physical pages into the GPU, or make the device able to
 * access the returned addresses. Callers must establish page-table mappings
 * with a separately verified architecture-specific implementation.
 */
#define GPU_VA_PAGE_SIZE       4096ULL
#define GPU_VA_MAX_FREE_RANGES 1024U
#define GPU_VA_MAX_ALLOCS      512U
#define GPU_VA_GUARD_NONE      0U
#define GPU_VA_GUARD_ONE_PAGE  1U

typedef enum gpu_va_status {
    GPU_VA_OK = 0,
    GPU_VA_INVALID_ARGUMENT,
    GPU_VA_INVALID_RANGE,
    GPU_VA_NOT_INITIALIZED,
    GPU_VA_OUT_OF_SPACE,
    GPU_VA_FRAGMENTED,
    GPU_VA_TABLE_FULL,
    GPU_VA_INVALID_HANDLE,
    GPU_VA_DOUBLE_FREE,
    GPU_VA_CORRUPT_STATE
} gpu_va_status_t;

typedef struct gpu_va_range {
    uint64_t base;
    uint64_t size;
} gpu_va_range_t;

typedef struct gpu_va_allocation {
    uint64_t base;
    uint64_t size;
    uint64_t reserved_base;
    uint64_t reserved_size;
    uint64_t token;
    uint32_t generation;
    uint8_t guard_pages;
    uint8_t valid;
} gpu_va_allocation_t;

typedef struct gpu_va_record {
    uint64_t reserved_base;
    uint64_t reserved_size;
    uint64_t token;
    uint32_t generation;
    uint8_t active;
} gpu_va_record_t;

typedef struct gpu_va_space {
    spinlock_t lock;
    gpu_va_range_t free_ranges[GPU_VA_MAX_FREE_RANGES];
    gpu_va_record_t allocations[GPU_VA_MAX_ALLOCS];
    size_t free_count;
    size_t allocation_count;
    uint64_t start;
    uint64_t end;
    uint64_t next_token;
    uint32_t generation;
    uint8_t initialized;
} gpu_va_space_t;

/* Zero-initialize before first init; the range is [start, start + size). */
gpu_va_status_t gpu_va_init(gpu_va_space_t *space,
                                  uint64_t start, uint64_t size);

/* size/alignment must be nonzero; alignment must be a power of two. */
gpu_va_status_t gpu_va_alloc(gpu_va_space_t *space,
                                   uint64_t size, uint64_t alignment,
                                   unsigned guard_pages,
                                   gpu_va_allocation_t *out_allocation);

/* Free validates the opaque token, generation, and exact reserved range. */
gpu_va_status_t gpu_va_free(gpu_va_space_t *space,
                                  gpu_va_allocation_t *allocation);

/* Returns a locked, internally consistent snapshot of allocator accounting. */
gpu_va_status_t gpu_va_get_stats(gpu_va_space_t *space,
                                       uint64_t *free_bytes,
                                       size_t *free_range_count,
                                       size_t *allocation_count);

/* Expensive invariant check intended for tests and diagnostic builds. */
gpu_va_status_t gpu_va_validate(gpu_va_space_t *space);

#endif
