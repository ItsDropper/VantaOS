/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_DMA_H
#define VANTA_GPU_DMA_H

#include <stddef.h>
#include <stdint.h>
#include "dma.h"

#define GPU_DMA_PAGE_SIZE 4096U
#define GPU_DMA_MAX_PAGES DMA_MAX_CONTIGUOUS_PAGES
#define GPU_DMA_MAX_BYTES ((size_t)GPU_DMA_PAGE_SIZE * GPU_DMA_MAX_PAGES)

typedef enum gpu_dma_status {
    GPU_DMA_OK = 0,
    GPU_DMA_INVALID_ARGUMENT,
    GPU_DMA_TOO_LARGE,
    GPU_DMA_ALLOCATION_FAILED,
    GPU_DMA_INVALID_PAGE,
    GPU_DMA_NONCONTIGUOUS,
    GPU_DMA_ADDRESS_WIDTH_UNSUPPORTED,
    GPU_DMA_ADDRESS_RANGE_INVALID
} gpu_dma_status_t;

typedef struct gpu_dma_buffer {
    dma_page_t *pages;
    void *cpu_address;
    uint64_t gpu_address;
    size_t size;
    size_t allocated_size;
    size_t page_count;
    uint64_t allocation_id;
    uint8_t acquired;
} gpu_dma_buffer_t;

/* Caller must zero-initialize the handle; allocations never enable bus mastering. */
gpu_dma_status_t gpu_dma_alloc(size_t size, unsigned address_bits,
                               gpu_dma_buffer_t *out_buffer);
void gpu_dma_release(gpu_dma_buffer_t *buffer);
int gpu_dma_buffer_is_valid(const gpu_dma_buffer_t *buffer);

#endif
