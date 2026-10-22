/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_mmio.h"

#include <stdint.h>

static gpu_mmio_status_t gpu_mmio_validate(const gpu_mmio_region_t *region,
                                           size_t offset)
{
    uintptr_t base;

    if (region == NULL || region->base == NULL)
        return GPU_MMIO_INVALID_ARGUMENT;
    if (offset > region->length ||
        sizeof(uint32_t) > region->length - offset)
        return GPU_MMIO_OUT_OF_RANGE;

    base = (uintptr_t)region->base;
    if (base > UINTPTR_MAX - offset)
        return GPU_MMIO_OUT_OF_RANGE;
    if (((base + offset) & (sizeof(uint32_t) - 1U)) != 0U)
        return GPU_MMIO_UNALIGNED;
    return GPU_MMIO_OK;
}

gpu_mmio_status_t gpu_mmio_read32(const gpu_mmio_region_t *region,
                                  size_t offset, uint32_t *out_value)
{
    gpu_mmio_status_t status;

    if (out_value == NULL)
        return GPU_MMIO_INVALID_ARGUMENT;
    *out_value = 0U;
    status = gpu_mmio_validate(region, offset);
    if (status != GPU_MMIO_OK)
        return status;

    /* Mapping attributes and ordering beyond volatile are platform-owned. */
    *out_value = *(volatile uint32_t *)(void *)(region->base + offset);
    return GPU_MMIO_OK;
}

gpu_mmio_status_t gpu_mmio_write32(const gpu_mmio_region_t *region,
                                   size_t offset, uint32_t value)
{
    gpu_mmio_status_t status = gpu_mmio_validate(region, offset);

    if (status != GPU_MMIO_OK)
        return status;
    *(volatile uint32_t *)(void *)(region->base + offset) = value;
    return GPU_MMIO_OK;
}
