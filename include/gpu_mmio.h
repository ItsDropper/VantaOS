/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_MMIO_H
#define VANTA_GPU_MMIO_H

#include <stddef.h>
#include <stdint.h>

typedef enum gpu_mmio_status {
    GPU_MMIO_OK = 0,
    GPU_MMIO_INVALID_ARGUMENT,
    GPU_MMIO_UNALIGNED,
    GPU_MMIO_OUT_OF_RANGE
} gpu_mmio_status_t;

typedef struct gpu_mmio_region {
    volatile uint8_t *base;
    size_t length;
} gpu_mmio_region_t;

/* The caller owns mapping lifetime and device-specific register semantics. */
gpu_mmio_status_t gpu_mmio_read32(const gpu_mmio_region_t *region,
                                  size_t offset, uint32_t *out_value);
gpu_mmio_status_t gpu_mmio_write32(const gpu_mmio_region_t *region,
                                   size_t offset, uint32_t value);

#endif
