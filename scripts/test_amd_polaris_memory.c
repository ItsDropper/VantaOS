/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_memory.h"
#include "heap.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MOCK_MAPS 32U
typedef struct mock_map {
    uint64_t va;
    uint64_t pa;
    int active;
} mock_map_t;

static uint8_t dma_pool[1024U * 1024U]
    __attribute__((aligned(GPU_DMA_PAGE_SIZE)));
static size_t dma_offset;
static mock_map_t maps[MOCK_MAPS];
static uint64_t fail_map_va;
static unsigned fail_flushes;
static unsigned flush_count;

int heap_is_initialized(void) { return 1; }
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr) { free(ptr); }

int dma_alloc_contiguous(dma_page_t *pages, size_t count)
{
    size_t bytes = count * GPU_DMA_PAGE_SIZE;
    if (pages == NULL || count == 0U || bytes > sizeof(dma_pool) - dma_offset)
        return 0;
    memset(dma_pool + dma_offset, 0, bytes);
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = dma_pool + dma_offset +
                                   i * GPU_DMA_PAGE_SIZE;
        pages[i].physical_address = 0x20000000ULL + dma_offset +
                                    i * GPU_DMA_PAGE_SIZE;
        pages[i].size = GPU_DMA_PAGE_SIZE;
    }
    dma_offset += bytes;
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
    assert(pages != NULL && count != 0U);
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = NULL;
        pages[i].physical_address = 0U;
        pages[i].size = 0U;
    }
}

static int mock_map(void *context, uint64_t va, uint64_t pa, unsigned access)
{
    (void)context;
    if (va == fail_map_va ||
        (access & (GPU_VM_ACCESS_READ | GPU_VM_ACCESS_WRITE)) !=
            (GPU_VM_ACCESS_READ | GPU_VM_ACCESS_WRITE))
        return 0;
    for (size_t i = 0U; i < MOCK_MAPS; ++i) {
        if (maps[i].active && maps[i].va == va)
            return 0;
    }
    for (size_t i = 0U; i < MOCK_MAPS; ++i) {
        if (!maps[i].active) {
            maps[i].va = va;
            maps[i].pa = pa;
            maps[i].active = 1;
            return 1;
        }
    }
    return 0;
}

static int mock_unmap(void *context, uint64_t va)
{
    (void)context;
    for (size_t i = 0U; i < MOCK_MAPS; ++i) {
        if (maps[i].active && maps[i].va == va) {
            maps[i].active = 0;
            return 1;
        }
    }
    return 0;
}

static int mock_flush(void *context)
{
    (void)context;
    ++flush_count;
    if (fail_flushes != 0U) {
        --fail_flushes;
        return 0;
    }
    return 1;
}

static size_t active_maps(void)
{
    size_t count = 0U;
    for (size_t i = 0U; i < MOCK_MAPS; ++i)
        if (maps[i].active)
            ++count;
    return count;
}

int main(void)
{
    amd_polaris_memory_runtime_t runtime = {0};
    amd_polaris_gpu_allocation_t a = {0};
    amd_polaris_gpu_allocation_t b = {0};
    amd_polaris_gpu_allocation_t c = {0};
    gpu_vm_ops_t ops = {
        .context = &runtime,
        .map_page = mock_map,
        .unmap_page = mock_unmap,
        .flush = mock_flush
    };

    assert(amd_polaris_memory_init(&runtime, &ops) == AMD_POLARIS_MEMORY_OK);
    assert(amd_polaris_memory_alloc(&runtime, 6000U, 0U, &a) ==
           AMD_POLARIS_MEMORY_OK);
    assert(a.gpu_address == AMD_POLARIS_MEMORY_VA_BASE);
    assert(a.page_count == 2U && active_maps() == 2U);
    assert(amd_polaris_memory_alloc(&runtime, 4096U, 0U, &b) ==
           AMD_POLARIS_MEMORY_OK);
    assert(b.gpu_address == a.gpu_address + a.dma.allocated_size);
    assert(amd_polaris_memory_alloc(&runtime, 4096U, a.gpu_address, &c) ==
           AMD_POLARIS_MEMORY_VA_BUSY);
    assert(c.dma.acquired == 0U);

    assert(amd_polaris_memory_free(&a) == AMD_POLARIS_MEMORY_OK);
    assert(active_maps() == 1U);
    assert(amd_polaris_memory_alloc(&runtime, 4096U, 0U, &c) ==
           AMD_POLARIS_MEMORY_OK);
    assert(c.gpu_address == AMD_POLARIS_MEMORY_VA_BASE);

    /* A partial map must roll back its mapped prefix before freeing DMA. */
    fail_map_va = AMD_POLARIS_MEMORY_VA_BASE + 0x3000U;
    {
        amd_polaris_gpu_allocation_t failed = {0};
        assert(amd_polaris_memory_alloc(&runtime, 8192U, 0x100003000ULL,
            &failed) == AMD_POLARIS_MEMORY_MAP_FAILED);
        assert(failed.dma.acquired == 0U);
        assert(active_maps() == 3U);
    }
    fail_map_va = 0U;

    assert(amd_polaris_memory_free(&b) == AMD_POLARIS_MEMORY_OK);
    assert(amd_polaris_memory_free(&c) == AMD_POLARIS_MEMORY_OK);
    assert(active_maps() == 0U);

    /* If TLB invalidation fails, keep the DMA pinned until retry succeeds. */
    fail_flushes = 1U;
    {
        amd_polaris_gpu_allocation_t pinned = {0};
        assert(amd_polaris_memory_alloc(&runtime, 4096U, 0U, &pinned) ==
               AMD_POLARIS_MEMORY_FLUSH_FAILED);
        assert(pinned.dma.acquired != 0U && pinned.mapped != 0U);
        assert(amd_polaris_memory_free(&pinned) == AMD_POLARIS_MEMORY_OK);
        assert(pinned.dma.acquired == 0U);
    }
    assert(active_maps() == 0U);
    assert(amd_polaris_memory_shutdown(&runtime, 0) ==
           AMD_POLARIS_MEMORY_NOT_READY);
    assert(amd_polaris_memory_shutdown(&runtime, 1) == AMD_POLARIS_MEMORY_OK);
    return 0;
}
