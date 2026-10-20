/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_dma.h"
#include "heap.h"

#include <limits.h>
#include <string.h>

static uint64_t nvx_dma_allocation_counter;

static void nvx_dma_clear(nvx_dma_buffer_t *buffer)
{
    buffer->pages = 0;
    buffer->cpu_address = 0;
    buffer->gpu_address = 0;
    buffer->size = 0;
    buffer->allocated_size = 0;
    buffer->page_count = 0;
    buffer->allocation_id = 0U;
    buffer->acquired = 0;
}

nvx_dma_status_t nvx_dma_alloc(size_t size, unsigned address_bits,
                               nvx_dma_buffer_t *out_buffer)
{
    size_t page_count;
    size_t allocated_size;

    if (out_buffer == 0)
        return NVX_DMA_INVALID_ARGUMENT;
    if (out_buffer->acquired || out_buffer->page_count != 0U ||
        out_buffer->pages != 0)
        return NVX_DMA_INVALID_ARGUMENT;

    nvx_dma_clear(out_buffer);
    if (size == 0U)
        return NVX_DMA_INVALID_ARGUMENT;
    if (address_bits != 32U && address_bits != 64U)
        return NVX_DMA_ADDRESS_WIDTH_UNSUPPORTED;
    if (size > NVX_DMA_MAX_BYTES)
        return NVX_DMA_TOO_LARGE;

    page_count = (size + NVX_DMA_PAGE_SIZE - 1U) / NVX_DMA_PAGE_SIZE;
    allocated_size = page_count * NVX_DMA_PAGE_SIZE;
    if (page_count == 0U || page_count > NVX_DMA_MAX_PAGES ||
        allocated_size < size || page_count > SIZE_MAX / sizeof(dma_page_t))
        return NVX_DMA_TOO_LARGE;
    if (!heap_is_initialized())
        return NVX_DMA_ALLOCATION_FAILED;

    out_buffer->pages = kmalloc(page_count * sizeof(dma_page_t));
    if (out_buffer->pages == 0) {
        nvx_dma_clear(out_buffer);
        return NVX_DMA_ALLOCATION_FAILED;
    }
    memset(out_buffer->pages, 0, page_count * sizeof(dma_page_t));
    if (!dma_alloc_contiguous(out_buffer->pages, page_count)) {
        kfree(out_buffer->pages);
        nvx_dma_clear(out_buffer);
        return NVX_DMA_ALLOCATION_FAILED;
    }

    for (size_t i = 0; i < page_count; ++i) {
        const dma_page_t *page = &out_buffer->pages[i];
        if (!dma_page_is_valid(page)) {
            dma_free_contiguous(out_buffer->pages, page_count);
            kfree(out_buffer->pages);
            nvx_dma_clear(out_buffer);
            return NVX_DMA_INVALID_PAGE;
        }
        if (i > 0U &&
            (page->physical_address !=
                 out_buffer->pages[i - 1U].physical_address +
                     NVX_DMA_PAGE_SIZE ||
             (uintptr_t)page->virtual_address !=
                 (uintptr_t)out_buffer->pages[i - 1U].virtual_address +
                     NVX_DMA_PAGE_SIZE)) {
            dma_free_contiguous(out_buffer->pages, page_count);
            kfree(out_buffer->pages);
            nvx_dma_clear(out_buffer);
            return NVX_DMA_NONCONTIGUOUS;
        }
    }

    out_buffer->cpu_address = out_buffer->pages[0].virtual_address;
    out_buffer->gpu_address = out_buffer->pages[0].physical_address;
    out_buffer->size = size;
    out_buffer->allocated_size = allocated_size;
    out_buffer->page_count = page_count;

    if (out_buffer->gpu_address >
            UINT64_MAX - ((uint64_t)allocated_size - 1U) ||
        (address_bits == 32U &&
         (out_buffer->gpu_address > UINT32_MAX ||
          (uint64_t)allocated_size - 1U >
              UINT32_MAX - out_buffer->gpu_address))) {
        dma_free_contiguous(out_buffer->pages, page_count);
        kfree(out_buffer->pages);
        nvx_dma_clear(out_buffer);
        return NVX_DMA_ADDRESS_RANGE_INVALID;
    }

    out_buffer->allocation_id = __atomic_add_fetch(
        &nvx_dma_allocation_counter, 1U, __ATOMIC_RELAXED);
    if (out_buffer->allocation_id == 0U) {
        __atomic_store_n(&nvx_dma_allocation_counter, UINT64_MAX, __ATOMIC_RELAXED);
        dma_free_contiguous(out_buffer->pages, page_count);
        kfree(out_buffer->pages);
        nvx_dma_clear(out_buffer);
        return NVX_DMA_ALLOCATION_FAILED;
    }
    out_buffer->acquired = 1U;
    return NVX_DMA_OK;
}

void nvx_dma_release(nvx_dma_buffer_t *buffer)
{
    if (buffer == 0 || !buffer->acquired)
        return;

    dma_free_contiguous(buffer->pages, buffer->page_count);
    kfree(buffer->pages);
    nvx_dma_clear(buffer);
}
