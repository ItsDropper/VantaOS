/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_dma.h"
#include "gpu_dma.h"

static nvx_dma_status_t nvx_dma_translate_status(gpu_dma_status_t status)
{
    switch (status) {
        case GPU_DMA_OK: return NVX_DMA_OK;
        case GPU_DMA_INVALID_ARGUMENT: return NVX_DMA_INVALID_ARGUMENT;
        case GPU_DMA_TOO_LARGE: return NVX_DMA_TOO_LARGE;
        case GPU_DMA_ALLOCATION_FAILED: return NVX_DMA_ALLOCATION_FAILED;
        case GPU_DMA_INVALID_PAGE: return NVX_DMA_INVALID_PAGE;
        case GPU_DMA_NONCONTIGUOUS: return NVX_DMA_NONCONTIGUOUS;
        case GPU_DMA_ADDRESS_WIDTH_UNSUPPORTED:
            return NVX_DMA_ADDRESS_WIDTH_UNSUPPORTED;
        case GPU_DMA_ADDRESS_RANGE_INVALID:
            return NVX_DMA_ADDRESS_RANGE_INVALID;
        default: return NVX_DMA_ALLOCATION_FAILED;
    }
}

nvx_dma_status_t nvx_dma_alloc(size_t size, unsigned address_bits,
                               nvx_dma_buffer_t *out_buffer)
{
    return nvx_dma_translate_status(
        gpu_dma_alloc(size, address_bits, (gpu_dma_buffer_t *)out_buffer));
}

void nvx_dma_release(nvx_dma_buffer_t *buffer)
{
    gpu_dma_release((gpu_dma_buffer_t *)buffer);
}
