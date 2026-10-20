/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_dma.h"
#include "heap.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static unsigned alloc_calls;
static unsigned free_calls;
static int fail_alloc;
static int invalid_page;
static int noncontiguous;
static int high_address;
static int heap_ready = 1;
static int fail_metadata_alloc;
static unsigned metadata_allocs;
static unsigned metadata_frees;
static uint8_t memory[NVX_DMA_MAX_BYTES + NVX_DMA_PAGE_SIZE]
    __attribute__((aligned(NVX_DMA_PAGE_SIZE)));

int heap_is_initialized(void) { return heap_ready; }
void *kmalloc(size_t size)
{
    if (fail_metadata_alloc)
        return 0;
    void *p = malloc(size);
    if (p)
        ++metadata_allocs;
    return p;
}
void kfree(void *ptr)
{
    if (ptr) {
        ++metadata_frees;
        free(ptr);
    }
}

int dma_alloc_contiguous(dma_page_t *pages, size_t count)
{
    ++alloc_calls;
    if (fail_alloc || pages == 0 || count == 0U ||
        count > NVX_DMA_MAX_PAGES)
        return 0;
    memset(memory, 0, count * NVX_DMA_PAGE_SIZE);
    for (size_t i = 0; i < count; ++i) {
        pages[i].virtual_address = memory + i * NVX_DMA_PAGE_SIZE;
        pages[i].physical_address =
            (high_address ? 0x100000000ULL : 0x20000000ULL) +
            i * NVX_DMA_PAGE_SIZE;
        pages[i].size = NVX_DMA_PAGE_SIZE;
    }
    if (invalid_page)
        pages[0].size = 1U;
    if (noncontiguous && count > 1U)
        pages[1].physical_address += NVX_DMA_PAGE_SIZE;
    return 1;
}

int dma_page_is_valid(const dma_page_t *page)
{
    return page != 0 && page->virtual_address != 0 &&
           page->size == NVX_DMA_PAGE_SIZE &&
           (((uintptr_t)page->virtual_address &
             (NVX_DMA_PAGE_SIZE - 1U)) == 0U) &&
           ((page->physical_address & (NVX_DMA_PAGE_SIZE - 1U)) == 0U);
}

void dma_free_contiguous(dma_page_t *pages, size_t count)
{
    assert(pages != 0 && count > 0U && count <= NVX_DMA_MAX_PAGES);
    ++free_calls;
    for (size_t i = 0; i < count; ++i) {
        pages[i].virtual_address = 0;
        pages[i].physical_address = 0;
        pages[i].size = 0;
    }
}

int main(void)
{
    nvx_dma_buffer_t buffer = {0};

    assert(nvx_dma_alloc(0U, 32U, &buffer) == NVX_DMA_INVALID_ARGUMENT);
    assert(nvx_dma_alloc(1U, 48U, &buffer) ==
           NVX_DMA_ADDRESS_WIDTH_UNSUPPORTED);
    assert(nvx_dma_alloc(NVX_DMA_MAX_BYTES + 1U, 64U, &buffer) ==
           NVX_DMA_TOO_LARGE);
    assert(nvx_dma_alloc(1U, 32U, 0) == NVX_DMA_INVALID_ARGUMENT);

    heap_ready = 0;
    assert(nvx_dma_alloc(4096U, 32U, &buffer) ==
           NVX_DMA_ALLOCATION_FAILED);
    heap_ready = 1;
    fail_metadata_alloc = 1;
    assert(nvx_dma_alloc(4096U, 32U, &buffer) ==
           NVX_DMA_ALLOCATION_FAILED);
    assert(buffer.pages == 0 && !buffer.acquired);
    fail_metadata_alloc = 0;

    fail_alloc = 1;
    assert(nvx_dma_alloc(4096U, 32U, &buffer) ==
           NVX_DMA_ALLOCATION_FAILED);
    assert(!buffer.acquired && buffer.cpu_address == 0);
    fail_alloc = 0;

    invalid_page = 1;
    assert(nvx_dma_alloc(4096U, 32U, &buffer) == NVX_DMA_INVALID_PAGE);
    assert(free_calls == 1U && !buffer.acquired);
    invalid_page = 0;

    noncontiguous = 1;
    assert(nvx_dma_alloc(8192U, 64U, &buffer) ==
           NVX_DMA_NONCONTIGUOUS);
    assert(free_calls == 2U && !buffer.acquired);
    noncontiguous = 0;

    high_address = 1;
    assert(nvx_dma_alloc(4096U, 32U, &buffer) ==
           NVX_DMA_ADDRESS_RANGE_INVALID);
    assert(free_calls == 3U && !buffer.acquired);
    assert(nvx_dma_alloc(4096U, 64U, &buffer) == NVX_DMA_OK);
    assert(buffer.acquired && buffer.page_count == 1U);
    assert(buffer.size == 4096U && buffer.allocated_size == 4096U);
    assert(buffer.gpu_address == 0x100000000ULL);
    nvx_dma_release(&buffer);
    assert(!buffer.acquired && buffer.cpu_address == 0 &&
           buffer.page_count == 0U);
    assert(free_calls == 4U);
    nvx_dma_release(&buffer);
    assert(free_calls == 4U);

    high_address = 0;
    assert(nvx_dma_alloc(4097U, 32U, &buffer) == NVX_DMA_OK);
    assert(buffer.page_count == 2U && buffer.size == 4097U &&
           buffer.allocated_size == 8192U);
    assert(buffer.gpu_address == 0x20000000ULL);
    nvx_dma_release(&buffer);
    assert(free_calls == 5U);

    /* Allocated handles cannot be overwritten by a second allocation. */
    assert(nvx_dma_alloc(4096U, 32U, &buffer) == NVX_DMA_OK);
    assert(nvx_dma_alloc(4096U, 32U, &buffer) == NVX_DMA_INVALID_ARGUMENT);
    assert(alloc_calls == 7U);
    nvx_dma_release(&buffer);
    assert(free_calls == 6U);

    /* Real GSP runtime images exceed the former 256 KiB ceiling. */
    assert(nvx_dma_alloc(NVX_DMA_MAX_BYTES, 64U, &buffer) == NVX_DMA_OK);
    assert(buffer.page_count == NVX_DMA_MAX_PAGES &&
           buffer.size == NVX_DMA_MAX_BYTES &&
           buffer.allocated_size == NVX_DMA_MAX_BYTES);
    assert(buffer.pages != 0 && buffer.gpu_address == 0x20000000ULL);
    nvx_dma_release(&buffer);
    assert(free_calls == 7U && buffer.pages == 0);
    assert(metadata_allocs == metadata_frees);
    return 0;
}
