/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GPUVA_H
#define VANTA_NVX_GPUVA_H

#include <stddef.h>
#include <stdint.h>
#include "spinlock.h"

/*
 * Software GPU virtual-address reservation allocator.
 *
 * This manages address-space ownership only. It does not construct NVIDIA
 * page tables, map physical pages into the GPU, or make the device able to
 * access the returned addresses. Callers must establish page-table mappings
 * with a separately verified architecture-specific implementation.
 */
#define NVX_GPUVA_PAGE_SIZE       4096ULL
#define NVX_GPUVA_MAX_FREE_RANGES 256U
#define NVX_GPUVA_MAX_ALLOCS      512U
#define NVX_GPUVA_GUARD_NONE      0U
#define NVX_GPUVA_GUARD_ONE_PAGE  1U

typedef enum nvx_gpuva_status {
    NVX_GPUVA_OK = 0,
    NVX_GPUVA_INVALID_ARGUMENT,
    NVX_GPUVA_INVALID_RANGE,
    NVX_GPUVA_NOT_INITIALIZED,
    NVX_GPUVA_OUT_OF_SPACE,
    NVX_GPUVA_FRAGMENTED,
    NVX_GPUVA_TABLE_FULL,
    NVX_GPUVA_INVALID_HANDLE,
    NVX_GPUVA_DOUBLE_FREE,
    NVX_GPUVA_CORRUPT_STATE
} nvx_gpuva_status_t;

typedef struct nvx_gpuva_range {
    uint64_t base;
    uint64_t size;
} nvx_gpuva_range_t;

typedef struct nvx_gpuva_allocation {
    uint64_t base;
    uint64_t size;
    uint64_t reserved_base;
    uint64_t reserved_size;
    uint64_t token;
    uint32_t generation;
    uint8_t guard_pages;
    uint8_t valid;
} nvx_gpuva_allocation_t;

typedef struct nvx_gpuva_record {
    uint64_t reserved_base;
    uint64_t reserved_size;
    uint64_t token;
    uint32_t generation;
    uint8_t active;
} nvx_gpuva_record_t;

typedef struct nvx_gpuva_space {
    spinlock_t lock;
    nvx_gpuva_range_t free_ranges[NVX_GPUVA_MAX_FREE_RANGES];
    nvx_gpuva_record_t allocations[NVX_GPUVA_MAX_ALLOCS];
    size_t free_count;
    size_t allocation_count;
    uint64_t start;
    uint64_t end;
    uint64_t next_token;
    uint32_t generation;
    uint8_t initialized;
} nvx_gpuva_space_t;

/* Space is initialized in place; the range is [start, start + size). */
nvx_gpuva_status_t nvx_gpuva_init(nvx_gpuva_space_t *space,
                                  uint64_t start, uint64_t size);

/* size/alignment must be nonzero; alignment must be a power of two. */
nvx_gpuva_status_t nvx_gpuva_alloc(nvx_gpuva_space_t *space,
                                   uint64_t size, uint64_t alignment,
                                   unsigned guard_pages,
                                   nvx_gpuva_allocation_t *out_allocation);

/* Free validates the opaque token, generation, and exact reserved range. */
nvx_gpuva_status_t nvx_gpuva_free(nvx_gpuva_space_t *space,
                                  nvx_gpuva_allocation_t *allocation);

/* Returns a locked, internally consistent snapshot of allocator accounting. */
nvx_gpuva_status_t nvx_gpuva_get_stats(nvx_gpuva_space_t *space,
                                       uint64_t *free_bytes,
                                       size_t *free_range_count,
                                       size_t *allocation_count);

/* Expensive invariant check intended for tests and diagnostic builds. */
nvx_gpuva_status_t nvx_gpuva_validate(nvx_gpuva_space_t *space);

#endif
