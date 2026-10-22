/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GPUVA_H
#define VANTA_NVX_GPUVA_H

#include "gpu_va.h"

#define NVX_GPUVA_PAGE_SIZE GPU_VA_PAGE_SIZE
#define NVX_GPUVA_MAX_FREE_RANGES GPU_VA_MAX_FREE_RANGES
#define NVX_GPUVA_MAX_ALLOCS GPU_VA_MAX_ALLOCS
#define NVX_GPUVA_GUARD_NONE GPU_VA_GUARD_NONE
#define NVX_GPUVA_GUARD_ONE_PAGE GPU_VA_GUARD_ONE_PAGE

typedef gpu_va_status_t nvx_gpuva_status_t;
#define NVX_GPUVA_OK GPU_VA_OK
#define NVX_GPUVA_INVALID_ARGUMENT GPU_VA_INVALID_ARGUMENT
#define NVX_GPUVA_INVALID_RANGE GPU_VA_INVALID_RANGE
#define NVX_GPUVA_NOT_INITIALIZED GPU_VA_NOT_INITIALIZED
#define NVX_GPUVA_OUT_OF_SPACE GPU_VA_OUT_OF_SPACE
#define NVX_GPUVA_FRAGMENTED GPU_VA_FRAGMENTED
#define NVX_GPUVA_TABLE_FULL GPU_VA_TABLE_FULL
#define NVX_GPUVA_INVALID_HANDLE GPU_VA_INVALID_HANDLE
#define NVX_GPUVA_DOUBLE_FREE GPU_VA_DOUBLE_FREE
#define NVX_GPUVA_CORRUPT_STATE GPU_VA_CORRUPT_STATE

typedef gpu_va_range_t nvx_gpuva_range_t;
typedef gpu_va_allocation_t nvx_gpuva_allocation_t;
typedef gpu_va_record_t nvx_gpuva_record_t;
typedef gpu_va_space_t nvx_gpuva_space_t;

nvx_gpuva_status_t nvx_gpuva_init(nvx_gpuva_space_t *space,
                                  uint64_t start, uint64_t size);
nvx_gpuva_status_t nvx_gpuva_alloc(nvx_gpuva_space_t *space,
                                   uint64_t size, uint64_t alignment,
                                   unsigned guard_pages,
                                   nvx_gpuva_allocation_t *out_allocation);
nvx_gpuva_status_t nvx_gpuva_free(nvx_gpuva_space_t *space,
                                  nvx_gpuva_allocation_t *allocation);
nvx_gpuva_status_t nvx_gpuva_get_stats(nvx_gpuva_space_t *space,
                                       uint64_t *free_bytes,
                                       size_t *free_range_count,
                                       size_t *allocation_count);
nvx_gpuva_status_t nvx_gpuva_validate(nvx_gpuva_space_t *space);

#endif
