/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_DMA_H
#define VANTA_NVX_DMA_H

#include <stddef.h>
#include <stdint.h>
#include "dma.h"

#define NVX_DMA_PAGE_SIZE 4096U
#define NVX_DMA_MAX_PAGES 64U
#define NVX_DMA_MAX_BYTES ((size_t)NVX_DMA_PAGE_SIZE * NVX_DMA_MAX_PAGES)

typedef enum nvx_dma_status {
    NVX_DMA_OK = 0,
    NVX_DMA_INVALID_ARGUMENT,
    NVX_DMA_TOO_LARGE,
    NVX_DMA_ALLOCATION_FAILED,
    NVX_DMA_INVALID_PAGE,
    NVX_DMA_NONCONTIGUOUS,
    NVX_DMA_ADDRESS_WIDTH_UNSUPPORTED,
    NVX_DMA_ADDRESS_RANGE_INVALID
} nvx_dma_status_t;

/*
 * One physically contiguous, page-backed DMA allocation. The CPU address is
 * identity-mapped under the current platform DMA contract; gpu_address is the
 * bus-visible physical address. Allocation never enables PCI bus mastering.
 */
typedef struct nvx_dma_buffer {
    dma_page_t pages[NVX_DMA_MAX_PAGES];
    void *cpu_address;
    uint64_t gpu_address;
    size_t size;
    size_t allocated_size;
    size_t page_count;
    uint64_t allocation_id;
    uint8_t acquired;
} nvx_dma_buffer_t;

/* Zero-initialize buffer before first use; release it before reuse. */
nvx_dma_status_t nvx_dma_alloc(size_t size, unsigned address_bits,
                               nvx_dma_buffer_t *out_buffer);
void nvx_dma_release(nvx_dma_buffer_t *buffer);

#endif
