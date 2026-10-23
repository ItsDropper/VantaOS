/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_dma_page_table.h"

#include "heap.h"

#include <stdint.h>
#include <string.h>

int gpu_dma_page_table_allocate(void *context, size_t entry_count,
                                gpu_page_table_page_t *out_page)
{
    gpu_dma_buffer_t *buffer;
    size_t bytes;

    (void)context;
    if (out_page == NULL || out_page->entries != NULL ||
        out_page->cookie != NULL || entry_count == 0U ||
        entry_count > SIZE_MAX / sizeof(uint64_t))
        return 0;

    bytes = entry_count * sizeof(uint64_t);
    if (bytes > GPU_DMA_MAX_BYTES)
        return 0;

    buffer = kmalloc(sizeof(*buffer));
    if (buffer == NULL)
        return 0;
    memset(buffer, 0, sizeof(*buffer));

    if (gpu_dma_alloc(bytes, 32U, buffer) != GPU_DMA_OK) {
        kfree(buffer);
        return 0;
    }

    if (!gpu_dma_buffer_is_valid(buffer) ||
        buffer->allocated_size < bytes ||
        (buffer->gpu_address & (GPU_PAGE_TABLE_PAGE_SIZE - 1U)) != 0U) {
        gpu_dma_release(buffer);
        kfree(buffer);
        return 0;
    }

    out_page->entries = (uint64_t *)buffer->cpu_address;
    out_page->physical_address = buffer->gpu_address;
    out_page->entry_count = entry_count;
    out_page->allocation_size = buffer->allocated_size;
    out_page->cookie = buffer;
    return 1;
}

void gpu_dma_page_table_free(void *context, gpu_page_table_page_t *page)
{
    gpu_dma_buffer_t *buffer;

    (void)context;
    if (page == NULL || page->cookie == NULL)
        return;

    buffer = (gpu_dma_buffer_t *)page->cookie;
    if (!gpu_dma_buffer_is_valid(buffer) ||
        page->entries != (uint64_t *)buffer->cpu_address ||
        page->physical_address != buffer->gpu_address ||
        page->entry_count > SIZE_MAX / sizeof(uint64_t) ||
        page->entry_count * sizeof(uint64_t) > buffer->size)
        return;

    gpu_dma_release(buffer);
    kfree(buffer);
    memset(page, 0, sizeof(*page));
}
