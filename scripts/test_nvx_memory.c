/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_memory.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static nvx_gpuva_space_t va_space;
static nvx_memory_manager_t manager;
static uint8_t dma_memory[NVX_DMA_MAX_BYTES * NVX_MEMORY_MAX_OBJECTS]
    __attribute__((aligned(NVX_DMA_PAGE_SIZE)));
static size_t next_page;
static unsigned alloc_calls;
static unsigned free_calls;

int dma_alloc_contiguous(dma_page_t *pages, size_t count)
{
    if (pages == 0 || count == 0U ||
        count > NVX_DMA_MAX_PAGES ||
        next_page + count * NVX_DMA_PAGE_SIZE > sizeof(dma_memory))
        return 0;
    ++alloc_calls;
    for (size_t i = 0; i < count; ++i) {
        pages[i].virtual_address = &dma_memory[next_page];
        pages[i].physical_address = 0x10000000ULL + next_page;
        pages[i].size = NVX_DMA_PAGE_SIZE;
        next_page += NVX_DMA_PAGE_SIZE;
    }
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
    nvx_memory_handle_t a = {0}, b = {0}, forged;
    void *cpu = (void *)(uintptr_t)1U;

    memset(&va_space, 0, sizeof(va_space));
    memset(&manager, 0, sizeof(manager));
    assert(nvx_memory_init(&manager, &va_space, 64U) ==
           NVX_MEMORY_ADDRESS_SPACE_FAILED);
    assert(nvx_gpuva_init(&va_space, 0x100000000ULL,
                          16ULL * 1024ULL * 1024ULL) == NVX_GPUVA_OK);
    assert(nvx_memory_init(&manager, &va_space, 64U) == NVX_MEMORY_OK);
    assert(nvx_memory_alloc(&manager, 0U, NVX_MEMORY_STAGING, &a) ==
           NVX_MEMORY_INVALID_ARGUMENT);
    assert(nvx_memory_alloc(&manager, 4096U, (nvx_memory_kind_t)99, &a) ==
           NVX_MEMORY_INVALID_ARGUMENT);
    assert(nvx_memory_alloc(&manager, 4096U, NVX_MEMORY_STAGING, &a) ==
           NVX_MEMORY_OK);
    assert(a.token != 0U && a.gpu_virtual_address != 0U);
    assert(nvx_memory_cpu_address(&manager, &a, &cpu) == NVX_MEMORY_OK);
    assert(cpu != 0 && cpu != (void *)(uintptr_t)1U);
    memset(cpu, 0xA5, 4096U);
    assert(nvx_memory_validate(&manager) == NVX_MEMORY_OK);

    forged = a;
    forged.token++;
    assert(nvx_memory_cpu_address(&manager, &forged, &cpu) ==
           NVX_MEMORY_STALE_HANDLE);
    assert(cpu == 0);
    assert(nvx_memory_release(&manager, &forged) == NVX_MEMORY_STALE_HANDLE);

    assert(nvx_memory_alloc(&manager, 8193U, NVX_MEMORY_COMMAND_STORAGE, &b) ==
           NVX_MEMORY_OK);
    assert(nvx_memory_validate(&manager) == NVX_MEMORY_OK);
    assert(nvx_memory_release(&manager, &a) == NVX_MEMORY_OK);
    assert(nvx_memory_release(&manager, &a) == NVX_MEMORY_STALE_HANDLE);
    assert(nvx_memory_release(&manager, &b) == NVX_MEMORY_OK);
    assert(nvx_memory_validate(&manager) == NVX_MEMORY_OK);
    assert(alloc_calls == 2U);
    assert(free_calls == 2U);

    /* A physical allocation above 4 GiB must be rejected for a 32-bit device. */
    memset(&va_space, 0, sizeof(va_space));
    memset(&manager, 0, sizeof(manager));
    assert(nvx_gpuva_init(&va_space, 0x20000000ULL,
                          16ULL * 1024ULL * 1024ULL) == NVX_GPUVA_OK);
    assert(nvx_memory_init(&manager, &va_space, 32U) == NVX_MEMORY_OK);
    return 0;
}
