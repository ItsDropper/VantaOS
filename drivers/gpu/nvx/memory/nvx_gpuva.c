/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gpuva.h"

nvx_gpuva_status_t nvx_gpuva_init(nvx_gpuva_space_t *space,
                                  uint64_t start, uint64_t size)
{
    return gpu_va_init(space, start, size);
}

nvx_gpuva_status_t nvx_gpuva_alloc(nvx_gpuva_space_t *space,
                                   uint64_t size, uint64_t alignment,
                                   unsigned guard_pages,
                                   nvx_gpuva_allocation_t *out_allocation)
{
    return gpu_va_alloc(space, size, alignment, guard_pages, out_allocation);
}

nvx_gpuva_status_t nvx_gpuva_free(nvx_gpuva_space_t *space,
                                  nvx_gpuva_allocation_t *allocation)
{
    return gpu_va_free(space, allocation);
}

nvx_gpuva_status_t nvx_gpuva_get_stats(nvx_gpuva_space_t *space,
                                       uint64_t *free_bytes,
                                       size_t *free_range_count,
                                       size_t *allocation_count)
{
    return gpu_va_get_stats(space, free_bytes, free_range_count,
                            allocation_count);
}

nvx_gpuva_status_t nvx_gpuva_validate(nvx_gpuva_space_t *space)
{
    return gpu_va_validate(space);
}
