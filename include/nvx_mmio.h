/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_MMIO_H
#define VANTA_NVX_MMIO_H

#include <stddef.h>
#include <stdint.h>

typedef enum nvx_mmio_status {
    NVX_MMIO_OK = 0,
    NVX_MMIO_INVALID_ARGUMENT,
    NVX_MMIO_OUT_OF_RANGE,
    NVX_MMIO_UNALIGNED
} nvx_mmio_status_t;

/*
 * A borrowed mapping of a resource already validated and mapped by the caller.
 * This type does not map physical addresses or establish resource ownership.
 */
typedef struct nvx_mmio_region {
    volatile uint8_t *base;
    size_t length;
} nvx_mmio_region_t;

/* Access only naturally aligned 32-bit registers fully contained in the region. */
nvx_mmio_status_t nvx_mmio_read32(
    const nvx_mmio_region_t *region, size_t offset, uint32_t *out_value);
nvx_mmio_status_t nvx_mmio_write32(
    const nvx_mmio_region_t *region, size_t offset, uint32_t value);

#endif
