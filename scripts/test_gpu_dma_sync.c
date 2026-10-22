/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_dma_sync.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint8_t memory[4096] __attribute__((aligned(4096)));
static dma_page_t page = { memory, 0x200000U, 4096U };

int dma_page_is_valid(const dma_page_t *p)
{
    return p != NULL && p->virtual_address != NULL && p->size == 4096U &&
           p->physical_address == 0x200000U;
}
int dma_alloc_contiguous(dma_page_t *pages, size_t count)
{
    if (pages == NULL || count != 1U) return 0;
    pages[0] = page;
    return 1;
}
void dma_free_contiguous(dma_page_t *pages, size_t count)
{
    (void)pages;
    (void)count;
}

int main(void)
{
    gpu_dma_buffer_t buffer = {
        .pages = &page, .cpu_address = memory, .gpu_address = 0x200000U,
        .size = 3000U, .allocated_size = 4096U, .page_count = 1U,
        .allocation_id = 9U, .acquired = 1U
    };
    gpu_dma_sync_t sync = {0};

    assert(gpu_dma_sync_init(NULL, &buffer) == GPU_DMA_SYNC_INVALID_ARGUMENT);
    assert(gpu_dma_sync_init(&sync, NULL) == GPU_DMA_SYNC_INVALID_ARGUMENT);
    assert(gpu_dma_sync_init(&sync, &buffer) == GPU_DMA_SYNC_OK);
    assert(sync.owner == GPU_DMA_OWNER_CPU);
    assert(gpu_dma_sync_for_device(&sync, 0U, 0U) == GPU_DMA_SYNC_INVALID_RANGE);
    assert(gpu_dma_sync_for_device(&sync, 3000U, 1U) == GPU_DMA_SYNC_INVALID_RANGE);
    assert(gpu_dma_sync_for_device(&sync, SIZE_MAX, 2U) == GPU_DMA_SYNC_INVALID_RANGE);
    assert(gpu_dma_sync_for_device(&sync, 8U, 128U) == GPU_DMA_SYNC_OK);
    assert(gpu_dma_sync_for_device(&sync, 0U, 1U) == GPU_DMA_SYNC_WRONG_OWNER);
    assert(gpu_dma_sync_destroy(&sync) == GPU_DMA_SYNC_WRONG_OWNER);
    assert(gpu_dma_sync_for_cpu(&sync, 0U, 3000U) == GPU_DMA_SYNC_OK);
    assert(gpu_dma_sync_for_cpu(&sync, 0U, 1U) == GPU_DMA_SYNC_WRONG_OWNER);
    assert(gpu_dma_sync_for_shared(&sync, 0U, 3000U) == GPU_DMA_SYNC_OK);
    assert(gpu_dma_sync_for_device(&sync, 0U, 1U) == GPU_DMA_SYNC_WRONG_OWNER);
    assert(gpu_dma_sync_for_cpu(&sync, 0U, 3000U) == GPU_DMA_SYNC_OK);
    uint64_t saved = buffer.allocation_id;
    buffer.allocation_id++;
    assert(gpu_dma_sync_for_device(&sync, 0U, 1U) == GPU_DMA_SYNC_BUFFER_INVALID);
    buffer.allocation_id = saved;
    assert(gpu_dma_sync_destroy(&sync) == GPU_DMA_SYNC_OK);
    assert(gpu_dma_sync_destroy(&sync) == GPU_DMA_SYNC_NOT_INITIALIZED);
    return 0;
}
