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
    /*
     * The kernel currently identity-maps physical memory only below 1 GiB.
     * gpu_dma_alloc() can allocate anywhere below the PMM's 4 GiB ceiling,
     * but its CPU pointer is the physical address. Passing a >1 GiB result
     * into firmware staging would dereference an unmapped kernel address.
     *
     * Keep the caller's device-width contract (32/64-bit) but conservatively
     * allocate from the identity-mapped 30-bit window until NVX has an
     * explicit high-memory CPU mapping backend.
     */
    if (!out_buffer)
        return NVX_DMA_INVALID_ARGUMENT;
    if (address_bits != 32U && address_bits != 64U)
        return NVX_DMA_ADDRESS_WIDTH_UNSUPPORTED;

    return nvx_dma_translate_status(gpu_dma_alloc_identity_mapped(
        size, (gpu_dma_buffer_t *)out_buffer));
}

nvx_dma_status_t nvx_dma_alloc_identity_mapped(
    size_t size, nvx_dma_buffer_t *out_buffer)
{
    return nvx_dma_translate_status(gpu_dma_alloc_identity_mapped(
        size, (gpu_dma_buffer_t *)out_buffer));
}

void nvx_dma_release(nvx_dma_buffer_t *buffer)
{
    gpu_dma_release((gpu_dma_buffer_t *)buffer);
}
