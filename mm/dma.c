/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "dma.h"
#include "pmm.h"

#include <string.h>

#define DMA_PAGE_SIZE 4096U
#define DMA_PHYSICAL_LIMIT 0x100000000ULL

static uint64_t dma_limit_exclusive(const dma_device_t* device)
{
    uint64_t mask = device ? device->dma_mask : DMA_DEFAULT_MASK;

    /* PMM currently manages only the first 4 GiB of physical address space. */
    if (mask >= DMA_PHYSICAL_LIMIT - 1U)
        return DMA_PHYSICAL_LIMIT;
    if (mask < DMA_PAGE_SIZE - 1U)
        return 0U;
    return mask + 1U;
}

int dma_page_is_valid_for_device(const dma_device_t* device,
                                 const dma_page_t* page)
{
    uint64_t mask;
    if (!page || !page->virtual_address ||
        page->size != DMA_PAGE_SIZE ||
        (page->physical_address & (DMA_PAGE_SIZE - 1U)) != 0U ||
        ((uintptr_t)page->virtual_address & (DMA_PAGE_SIZE - 1U)) != 0U ||
        (uint64_t)(uintptr_t)page->virtual_address != page->physical_address)
        return 0;

    mask = device ? device->dma_mask : DMA_DEFAULT_MASK;
    if (page->physical_address > mask ||
        DMA_PAGE_SIZE - 1U > mask - page->physical_address)
        return 0;
    return 1;
}

int dma_page_is_valid(const dma_page_t* page)
{
    return dma_page_is_valid_for_device(NULL, page);
}

int dma_alloc_page_for_device(const dma_device_t* device, dma_page_t* page)
{
    void* memory;
    uint64_t limit;
    uint64_t physical;

    if (!page)
        return 0;
    page->virtual_address = NULL;
    page->physical_address = 0U;
    page->size = 0U;
    if (!pmm_is_initialized())
        return 0;

    limit = dma_limit_exclusive(device);
    if (limit == 0U)
        return 0;
    memory = pmm_alloc_block_below(limit);
    if (!memory)
        return 0;

    physical = (uint64_t)(uintptr_t)memory;
    page->virtual_address = memory;
    page->physical_address = physical;
    page->size = DMA_PAGE_SIZE;
    if (!dma_page_is_valid_for_device(device, page))
    {
        pmm_free_block(memory);
        page->virtual_address = NULL;
        page->physical_address = 0U;
        page->size = 0U;
        return 0;
    }

    memset(memory, 0, DMA_PAGE_SIZE);
    return 1;
}

int dma_alloc_page(dma_page_t* page)
{
    return dma_alloc_page_for_device(NULL, page);
}

int dma_alloc_contiguous_for_device(const dma_device_t* device,
                                    dma_page_t* pages, size_t count)
{
    void* base_address = NULL;
    uint64_t base_physical;
    uint64_t limit;
    uint32_t page_count;

    if (!pages || count == 0U ||
        count > DMA_MAX_CONTIGUOUS_PAGES || count > UINT32_MAX)
        return 0;
    for (size_t i = 0U; i < count; ++i)
    {
        pages[i].virtual_address = NULL;
        pages[i].physical_address = 0U;
        pages[i].size = 0U;
    }

    if (!pmm_is_initialized())
        return 0;
    limit = dma_limit_exclusive(device);
    if (limit == 0U)
        return 0;
    page_count = (uint32_t)count;
    if (!pmm_alloc_contiguous_blocks_below(page_count, limit, &base_address) ||
        !base_address)
        return 0;

    base_physical = (uint64_t)(uintptr_t)base_address;
    for (size_t i = 0U; i < count; ++i)
    {
        uint64_t physical = base_physical + (uint64_t)i * DMA_PAGE_SIZE;
        pages[i].virtual_address = (void*)(uintptr_t)physical;
        pages[i].physical_address = physical;
        pages[i].size = DMA_PAGE_SIZE;
        if (!dma_page_is_valid_for_device(device, &pages[i]))
        {
            for (size_t j = 0U; j < count; ++j)
            {
                pmm_free_block((void*)(uintptr_t)(
                    base_physical + (uint64_t)j * DMA_PAGE_SIZE));
                pages[j].virtual_address = NULL;
                pages[j].physical_address = 0U;
                pages[j].size = 0U;
            }
            return 0;
        }
    }

    for (size_t i = 0U; i < count; ++i)
        memset(pages[i].virtual_address, 0, DMA_PAGE_SIZE);
    return 1;
}

int dma_alloc_contiguous(dma_page_t* pages, size_t count)
{
    return dma_alloc_contiguous_for_device(NULL, pages, count);
}

void dma_free_contiguous(dma_page_t* pages, size_t count)
{
    if (!pages || count > DMA_MAX_CONTIGUOUS_PAGES)
        return;
    for (size_t i = 0U; i < count; ++i)
        dma_free_page(&pages[i]);
}

void dma_free_page(dma_page_t* page)
{
    if (!dma_page_is_valid(page))
        return;
    pmm_free_block(page->virtual_address);
    page->virtual_address = NULL;
    page->physical_address = 0U;
    page->size = 0U;
}
