/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_mmio.h"

#include <stdint.h>

static nvx_mmio_status_t nvx_mmio_validate(
    const nvx_mmio_region_t *region, size_t offset)
{
    if (region == 0 || region->base == 0)
        return NVX_MMIO_INVALID_ARGUMENT;
    if ((((uintptr_t)region->base | (uintptr_t)offset) &
         (sizeof(uint32_t) - 1U)) != 0U)
        return NVX_MMIO_UNALIGNED;
    if (offset > region->length ||
        sizeof(uint32_t) > region->length - offset)
        return NVX_MMIO_OUT_OF_RANGE;
    return NVX_MMIO_OK;
}

nvx_mmio_status_t nvx_mmio_read32(
    const nvx_mmio_region_t *region, size_t offset, uint32_t *out_value)
{
    nvx_mmio_status_t status;

    if (out_value == 0)
        return NVX_MMIO_INVALID_ARGUMENT;
    *out_value = 0;
    status = nvx_mmio_validate(region, offset);
    if (status != NVX_MMIO_OK)
        return status;

    /*
     * Volatile prevents the compiler from eliding or merging device accesses.
     * Platform-specific ordering/cache attributes remain the mapping layer's
     * responsibility; this is not a substitute for DMA barriers.
     */
    *out_value = *(volatile uint32_t *)(void *)(region->base + offset);
    return NVX_MMIO_OK;
}

nvx_mmio_status_t nvx_mmio_write32(
    const nvx_mmio_region_t *region, size_t offset, uint32_t value)
{
    nvx_mmio_status_t status = nvx_mmio_validate(region, offset);

    if (status != NVX_MMIO_OK)
        return status;
    *(volatile uint32_t *)(void *)(region->base + offset) = value;
    return NVX_MMIO_OK;
}
