/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_dma_page_table.h"
#include "heap.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define POLARIS_LEAF_ENTRIES (1U << 22U)
#define POLARIS_LEAF_BYTES ((size_t)POLARIS_LEAF_ENTRIES * sizeof(uint64_t))

static unsigned char dma_memory[GPU_DMA_MAX_BYTES]
    __attribute__((aligned(GPU_DMA_PAGE_SIZE)));

int heap_is_initialized(void) { return 1; }
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr) { free(ptr); }

int dma_alloc_contiguous(dma_page_t *pages, size_t count)
{
    if (pages == NULL || count == 0U || count > GPU_DMA_MAX_PAGES)
        return 0;
    memset(dma_memory, 0, count * GPU_DMA_PAGE_SIZE);
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = dma_memory + i * GPU_DMA_PAGE_SIZE;
        pages[i].physical_address = 0x20000000ULL +
                                   (uint64_t)i * GPU_DMA_PAGE_SIZE;
        pages[i].size = GPU_DMA_PAGE_SIZE;
    }
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
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = NULL;
        pages[i].physical_address = 0U;
        pages[i].size = 0U;
    }
}

int main(void)
{
    gpu_page_table_page_t page = {0};

    assert(POLARIS_LEAF_BYTES == 32U * 1024U * 1024U);
    assert(GPU_DMA_MAX_BYTES >= POLARIS_LEAF_BYTES);
    assert(gpu_dma_page_table_allocate(NULL, POLARIS_LEAF_ENTRIES, &page));
    assert(page.entries == (uint64_t *)(void *)dma_memory);
    assert(page.entry_count == POLARIS_LEAF_ENTRIES);
    assert(page.allocation_size == POLARIS_LEAF_BYTES);
    assert(page.physical_address == 0x20000000ULL);
    assert(page.cookie != NULL);
    assert(page.entries[0] == 0U && page.entries[POLARIS_LEAF_ENTRIES - 1U] == 0U);

    gpu_dma_page_table_free(NULL, &page);
    assert(page.entries == NULL && page.cookie == NULL);
    assert(page.entry_count == 0U && page.allocation_size == 0U);

    assert(!gpu_dma_page_table_allocate(NULL, 0U, &page));
    assert(!gpu_dma_page_table_allocate(NULL, SIZE_MAX / sizeof(uint64_t) + 1U,
                                        &page));
    return 0;
}
