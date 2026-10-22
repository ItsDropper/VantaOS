/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_dma.h"
#include "heap.h"

#include <limits.h>
#include <string.h>

static uint64_t gpu_dma_allocation_counter;

static void gpu_dma_clear(gpu_dma_buffer_t *buffer)
{
    buffer->pages = NULL;
    buffer->cpu_address = NULL;
    buffer->gpu_address = 0U;
    buffer->size = 0U;
    buffer->allocated_size = 0U;
    buffer->page_count = 0U;
    buffer->allocation_id = 0U;
    buffer->acquired = 0U;
}

int gpu_dma_buffer_is_valid(const gpu_dma_buffer_t *buffer)
{
    if (buffer == NULL || buffer->acquired == 0U ||
        buffer->pages == NULL || buffer->cpu_address == NULL ||
        buffer->page_count == 0U || buffer->page_count > GPU_DMA_MAX_PAGES ||
        buffer->size == 0U || buffer->size > buffer->allocated_size ||
        buffer->allocated_size != buffer->page_count * GPU_DMA_PAGE_SIZE ||
        buffer->allocated_size == 0U ||
        buffer->gpu_address != buffer->pages[0].physical_address ||
        buffer->cpu_address != buffer->pages[0].virtual_address ||
        buffer->allocation_id == 0U ||
        buffer->gpu_address > UINT64_MAX - (buffer->allocated_size - 1U) ||
        (uintptr_t)buffer->cpu_address >
            UINTPTR_MAX - (buffer->allocated_size - 1U))
        return 0;

    for (size_t i = 0U; i < buffer->page_count; ++i) {
        const dma_page_t *page = &buffer->pages[i];
        uintptr_t virtual_address;
        if (!dma_page_is_valid(page) ||
            (page->physical_address & (GPU_DMA_PAGE_SIZE - 1U)) != 0U ||
            ((uintptr_t)page->virtual_address &
             (GPU_DMA_PAGE_SIZE - 1U)) != 0U)
            return 0;
        virtual_address = (uintptr_t)page->virtual_address;
        if (i != 0U) {
            const dma_page_t *previous = &buffer->pages[i - 1U];
            if (previous->physical_address >
                    UINT64_MAX - GPU_DMA_PAGE_SIZE ||
                (uintptr_t)previous->virtual_address >
                    UINTPTR_MAX - GPU_DMA_PAGE_SIZE ||
                page->physical_address !=
                    previous->physical_address + GPU_DMA_PAGE_SIZE ||
                virtual_address !=
                    (uintptr_t)previous->virtual_address + GPU_DMA_PAGE_SIZE)
                return 0;
        }
    }
    return 1;
}

gpu_dma_status_t gpu_dma_alloc(size_t size, unsigned address_bits,
                               gpu_dma_buffer_t *out_buffer)
{
    size_t page_count, allocated_size;

    if (out_buffer == NULL)
        return GPU_DMA_INVALID_ARGUMENT;
    if (out_buffer->acquired != 0U || out_buffer->page_count != 0U ||
        out_buffer->pages != NULL)
        return GPU_DMA_INVALID_ARGUMENT;

    gpu_dma_clear(out_buffer);
    if (size == 0U)
        return GPU_DMA_INVALID_ARGUMENT;
    if (address_bits != 32U && address_bits != 64U)
        return GPU_DMA_ADDRESS_WIDTH_UNSUPPORTED;
    if (size > GPU_DMA_MAX_BYTES)
        return GPU_DMA_TOO_LARGE;

    page_count = (size + GPU_DMA_PAGE_SIZE - 1U) / GPU_DMA_PAGE_SIZE;
    if (page_count == 0U || page_count > GPU_DMA_MAX_PAGES ||
        page_count > SIZE_MAX / sizeof(dma_page_t))
        return GPU_DMA_TOO_LARGE;
    allocated_size = page_count * GPU_DMA_PAGE_SIZE;
    if (allocated_size < size || !heap_is_initialized())
        return GPU_DMA_ALLOCATION_FAILED;

    out_buffer->pages = kmalloc(page_count * sizeof(dma_page_t));
    if (out_buffer->pages == NULL) {
        gpu_dma_clear(out_buffer);
        return GPU_DMA_ALLOCATION_FAILED;
    }
    memset(out_buffer->pages, 0, page_count * sizeof(dma_page_t));
    if (!dma_alloc_contiguous(out_buffer->pages, page_count)) {
        kfree(out_buffer->pages);
        gpu_dma_clear(out_buffer);
        return GPU_DMA_ALLOCATION_FAILED;
    }

    for (size_t i = 0U; i < page_count; ++i) {
        const dma_page_t *page = &out_buffer->pages[i];
        if (!dma_page_is_valid(page)) {
            dma_free_contiguous(out_buffer->pages, page_count);
            kfree(out_buffer->pages);
            gpu_dma_clear(out_buffer);
            return GPU_DMA_INVALID_PAGE;
        }
        if (i != 0U &&
            (page->physical_address !=
                 out_buffer->pages[i - 1U].physical_address +
                     GPU_DMA_PAGE_SIZE ||
             (uintptr_t)page->virtual_address !=
                 (uintptr_t)out_buffer->pages[i - 1U].virtual_address +
                     GPU_DMA_PAGE_SIZE)) {
            dma_free_contiguous(out_buffer->pages, page_count);
            kfree(out_buffer->pages);
            gpu_dma_clear(out_buffer);
            return GPU_DMA_NONCONTIGUOUS;
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
        gpu_dma_clear(out_buffer);
        return GPU_DMA_ADDRESS_RANGE_INVALID;
    }

    out_buffer->allocation_id = __atomic_add_fetch(
        &gpu_dma_allocation_counter, 1U, __ATOMIC_RELAXED);
    if (out_buffer->allocation_id == 0U) {
        __atomic_store_n(&gpu_dma_allocation_counter, UINT64_MAX,
                         __ATOMIC_RELAXED);
        dma_free_contiguous(out_buffer->pages, page_count);
        kfree(out_buffer->pages);
        gpu_dma_clear(out_buffer);
        return GPU_DMA_ALLOCATION_FAILED;
    }
    out_buffer->acquired = 1U;
    if (!gpu_dma_buffer_is_valid(out_buffer)) {
        dma_free_contiguous(out_buffer->pages, page_count);
        kfree(out_buffer->pages);
        gpu_dma_clear(out_buffer);
        return GPU_DMA_INVALID_PAGE;
    }
    return GPU_DMA_OK;
}

void gpu_dma_release(gpu_dma_buffer_t *buffer)
{
    if (buffer == NULL || buffer->acquired == 0U)
        return;
    if (buffer->pages != NULL && buffer->page_count != 0U &&
        buffer->page_count <= GPU_DMA_MAX_PAGES) {
        dma_free_contiguous(buffer->pages, buffer->page_count);
        kfree(buffer->pages);
    }
    gpu_dma_clear(buffer);
}
