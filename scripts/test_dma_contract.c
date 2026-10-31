/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "dma.h"

#include <assert.h>
#include <stdint.h>

/* Allocation paths are not exercised by this pure validation contract test. */
int pmm_is_initialized(void) { return 0; }
void* pmm_alloc_block(void) { return 0; }
void* pmm_alloc_block_below(uint64_t limit) { (void)limit; return 0; }
int pmm_alloc_contiguous_blocks(uint32_t count, void** base)
{ (void)count; (void)base; return 0; }
int pmm_alloc_contiguous_blocks_below(uint32_t count, uint64_t limit,
                                      void** base)
{ (void)count; (void)limit; (void)base; return 0; }
void pmm_free_block(void* page) { (void)page; }

int main(void)
{
    dma_page_t page = {
        .virtual_address = (void*)(uintptr_t)0x1000U,
        .physical_address = 0x1000U,
        .size = 4096U
    };
    dma_device_t device = { .dma_mask = 0x1FFFU };

    assert(dma_page_is_valid_for_device(&device, &page));
    device.dma_mask = 0x1FFEU;
    assert(!dma_page_is_valid_for_device(&device, &page));
    device.dma_mask = 0x0FFFU;
    assert(!dma_page_is_valid_for_device(&device, &page));
    device.dma_mask = UINT64_MAX;
    assert(dma_page_is_valid_for_device(&device, &page));
    assert(dma_page_is_valid(&page));

    page.physical_address = 0x1001U;
    page.virtual_address = (void*)(uintptr_t)0x1001U;
    assert(!dma_page_is_valid_for_device(&device, &page));

    page.physical_address = 0xFFFFF000ULL;
    page.virtual_address = (void*)(uintptr_t)0xFFFFF000ULL;
    assert(dma_page_is_valid(&page));

    page.physical_address = 0x100000000ULL;
    page.virtual_address = (void*)(uintptr_t)0x100000000ULL;
    assert(!dma_page_is_valid_for_device(&device, &page));
    assert(!dma_page_is_valid(&page));
    return 0;
}
