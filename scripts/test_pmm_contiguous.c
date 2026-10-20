/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "pmm.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    vanta_boot_info_t info;
    void *run = (void *)(uintptr_t)1U;
    uint32_t free_before;

    memset(&info, 0, sizeof(info));
    info.magic = VANTA_BOOT_INFO_MAGIC;
    info.struct_size = sizeof(info);
    info.version = VANTA_BOOT_INFO_VERSION;
    info.source = VANTA_BOOT_SOURCE_MULTIBOOT1;
    info.flags = VANTA_BOOT_FLAG_MEMORY_MAP_VALID |
                 VANTA_BOOT_FLAG_LEGACY_INFO_PRESENT;
    info.memory_region_count = 2U;
    info.kernel_image_start = 0x100000ULL;
    info.kernel_image_end = 0x101000ULL;
    info.legacy_info_address = 0x200000ULL;
    info.legacy_info_length = 0x1000ULL;
    info.legacy_mmap_address = 0x201000ULL;
    info.legacy_mmap_length = 0x1000ULL;
    info.memory_regions[0] = (vanta_memory_region_t){
        .base = 0x100000ULL,
        .length = 0xA000ULL,
        .type = VANTA_MEMORY_USABLE
    };
    info.memory_regions[1] = (vanta_memory_region_t){
        .base = 0x103000ULL,
        .length = 0x1000ULL,
        .type = VANTA_MEMORY_RESERVED
    };

    pmm_initialize(&info);
    assert(pmm_is_initialized());
    assert(pmm_get_free_blocks() == 8U);
    free_before = pmm_get_free_blocks();

    assert(!pmm_alloc_contiguous_blocks(0U, &run));
    assert(run == 0 && pmm_get_free_blocks() == free_before);
    assert(!pmm_alloc_contiguous_blocks(1U, 0));
    assert(!pmm_alloc_contiguous_blocks(7U, &run));
    assert(run == 0 && pmm_get_free_blocks() == free_before);

    assert(pmm_alloc_contiguous_blocks(4U, &run));
    assert(run == (void *)(uintptr_t)0x104000ULL);
    assert(pmm_get_free_blocks() == free_before - 4U);
    pmm_free_block(run);
    pmm_free_block((void *)((uintptr_t)run + PAGE_SIZE));
    pmm_free_block((void *)((uintptr_t)run + 2U * PAGE_SIZE));
    pmm_free_block((void *)((uintptr_t)run + 3U * PAGE_SIZE));
    assert(pmm_get_free_blocks() == free_before);

    /* A second allocation must reuse the same contiguous run after release. */
    run = 0;
    assert(pmm_alloc_contiguous_blocks(4U, &run));
    assert(run == (void *)(uintptr_t)0x104000ULL);
    for (uint32_t i = 0U; i < 4U; ++i)
        pmm_free_block((void *)((uintptr_t)run + (uintptr_t)i * PAGE_SIZE));
    assert(pmm_get_free_blocks() == free_before);

    void *single = pmm_alloc_block();
    assert(single == (void *)(uintptr_t)0x101000ULL);
    pmm_free_block(single);
    assert(pmm_get_free_blocks() == free_before);
    return 0;
}
