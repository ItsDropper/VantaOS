/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9_memory.h"
#include "heap.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define APERTURE_BYTES INTEL_GEN9_GTTMMADR_SIZE
static uint8_t aperture[APERTURE_BYTES];
static uint8_t dma_pool[1024U * 1024U]
    __attribute__((aligned(GPU_DMA_PAGE_SIZE)));
static size_t dma_offset;

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

static int mock_forcewake_acquire(void *context)
{
    return context != NULL;
}

static int mock_forcewake_release(void *context)
{
    return context != NULL;
}

int main(void)
{
    intel_gen9_memory_runtime_t runtime = {0};
    intel_gen9_gpu_allocation_t a = {0};
    intel_gen9_gpu_allocation_t b = {0};
    intel_gen9_gpu_allocation_t c = {0};
    uint64_t pte_a, pte_b;

    memset(aperture, 0, sizeof(aperture));
    runtime.mapping.region.base = aperture;
    runtime.mapping.region.length = sizeof(aperture);
    runtime.mapping.active = 1U;
    runtime.initialized = 1U;
    runtime.forcewake.context = &runtime;
    runtime.forcewake.acquire_render = mock_forcewake_acquire;
    runtime.forcewake.release_render = mock_forcewake_release;

    assert(intel_gen9_memory_alloc(&runtime, 6000U, 0U, &a) ==
           INTEL_GEN9_MEMORY_OK);
    assert(a.mapped != 0U && a.dma.acquired != 0U);
    assert(a.ggtt_address == INTEL_GEN9_MEMORY_DEFAULT_GGTT_BASE);
    assert(a.dma.allocated_size == 8192U);

    assert(intel_gen9_memory_alloc(&runtime, 4096U, 0U, &b) ==
           INTEL_GEN9_MEMORY_OK);
    assert(b.ggtt_address == a.ggtt_address + a.dma.allocated_size);
    assert(intel_gen9_memory_alloc(&runtime, 4096U, a.ggtt_address, &c) ==
           INTEL_GEN9_MEMORY_ADDRESS_BUSY);
    assert(c.dma.acquired == 0U);

    assert(gpu_mmio_read64(&runtime.mapping.region,
        INTEL_GEN9_GGTT_PTE_BASE +
        (size_t)(a.ggtt_address >> 12U) * sizeof(uint64_t), &pte_a) ==
        GPU_MMIO_OK);
    assert(gpu_mmio_read64(&runtime.mapping.region,
        INTEL_GEN9_GGTT_PTE_BASE +
        (size_t)(b.ggtt_address >> 12U) * sizeof(uint64_t), &pte_b) ==
        GPU_MMIO_OK);
    assert(pte_a == (a.dma.gpu_address | INTEL_GEN9_PTE_PRESENT));
    assert(pte_b == (b.dma.gpu_address | INTEL_GEN9_PTE_PRESENT));

    assert(intel_gen9_memory_free(&a) == INTEL_GEN9_MEMORY_OK);
    assert(a.mapped == 0U && a.dma.acquired == 0U);
    assert(intel_gen9_memory_alloc(&runtime, 4096U, 0U, &c) ==
           INTEL_GEN9_MEMORY_OK);
    assert(c.ggtt_address == INTEL_GEN9_MEMORY_DEFAULT_GGTT_BASE);
    assert(intel_gen9_memory_free(&b) == INTEL_GEN9_MEMORY_OK);
    assert(intel_gen9_memory_free(&c) == INTEL_GEN9_MEMORY_OK);
    assert(runtime.faulted == 0U);
    return 0;
}
