/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_dma.h"
#include "heap.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int fail_alloc;
static int bad_page;
static int noncontiguous;
static int high_address;
static int heap_ready = 1;
static unsigned alloc_count, free_count;
static unsigned char memory[GPU_DMA_MAX_BYTES + GPU_DMA_PAGE_SIZE]
    __attribute__((aligned(GPU_DMA_PAGE_SIZE)));

int heap_is_initialized(void) { return heap_ready; }
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr) { free(ptr); }

int dma_alloc_contiguous(dma_page_t *pages, size_t count)
{
    ++alloc_count;
    if (fail_alloc || pages == NULL || count == 0U ||
        count > GPU_DMA_MAX_PAGES)
        return 0;
    memset(memory, 0, count * GPU_DMA_PAGE_SIZE);
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = memory + i * GPU_DMA_PAGE_SIZE;
        pages[i].physical_address =
            (high_address ? 0x100000000ULL : 0x20000000ULL) +
            i * GPU_DMA_PAGE_SIZE;
        pages[i].size = GPU_DMA_PAGE_SIZE;
    }
    if (bad_page)
        pages[0].size = 64U;
    if (noncontiguous && count > 1U)
        pages[1].physical_address += GPU_DMA_PAGE_SIZE;
    return 1;
}

int dma_page_is_valid(const dma_page_t *page)
{
    return page != NULL && page->virtual_address != NULL &&
        page->size == GPU_DMA_PAGE_SIZE &&
        (((uintptr_t)page->virtual_address & (GPU_DMA_PAGE_SIZE - 1U)) == 0U) &&
        ((page->physical_address & (GPU_DMA_PAGE_SIZE - 1U)) == 0U);
}

void dma_free_contiguous(dma_page_t *pages, size_t count)
{
    assert(pages != NULL && count != 0U && count <= GPU_DMA_MAX_PAGES);
    ++free_count;
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = NULL;
        pages[i].physical_address = 0U;
        pages[i].size = 0U;
    }
}

int main(void)
{
    gpu_dma_buffer_t buffer = {0};

    assert(gpu_dma_alloc(0U, 32U, &buffer) == GPU_DMA_INVALID_ARGUMENT);
    assert(gpu_dma_alloc(1U, 48U, &buffer) ==
           GPU_DMA_ADDRESS_WIDTH_UNSUPPORTED);
    assert(gpu_dma_alloc(GPU_DMA_MAX_BYTES + 1U, 64U, &buffer) ==
           GPU_DMA_TOO_LARGE);
    heap_ready = 0;
    assert(gpu_dma_alloc(4096U, 32U, &buffer) == GPU_DMA_ALLOCATION_FAILED);
    heap_ready = 1;
    fail_alloc = 1;
    assert(gpu_dma_alloc(4096U, 32U, &buffer) == GPU_DMA_ALLOCATION_FAILED);
    fail_alloc = 0;
    bad_page = 1;
    assert(gpu_dma_alloc(4096U, 32U, &buffer) == GPU_DMA_INVALID_PAGE);
    bad_page = 0;
    noncontiguous = 1;
    assert(gpu_dma_alloc(8192U, 64U, &buffer) == GPU_DMA_NONCONTIGUOUS);
    noncontiguous = 0;
    high_address = 1;
    assert(gpu_dma_alloc(4096U, 32U, &buffer) ==
           GPU_DMA_ADDRESS_RANGE_INVALID);
    assert(gpu_dma_alloc(4096U, 64U, &buffer) == GPU_DMA_OK);
    assert(gpu_dma_buffer_is_valid(&buffer));
    assert(buffer.gpu_address == 0x100000000ULL);
    gpu_dma_release(&buffer);
    assert(!buffer.acquired && buffer.pages == NULL);
    assert(free_count == 4U);
    high_address = 0;
    assert(gpu_dma_alloc(4097U, 32U, &buffer) == GPU_DMA_OK);
    assert(buffer.page_count == 2U && buffer.allocated_size == 8192U);
    assert(gpu_dma_alloc(4096U, 32U, &buffer) == GPU_DMA_INVALID_ARGUMENT);
    gpu_dma_release(&buffer);
    assert(!gpu_dma_buffer_is_valid(&buffer));
    return 0;
}
