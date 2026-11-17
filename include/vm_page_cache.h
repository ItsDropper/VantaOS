/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 */
#ifndef VM_PAGE_CACHE_H
#define VM_PAGE_CACHE_H
#include <stdint.h>
#include "vm_region.h"
#define VM_PAGE_CACHE_MAX_ENTRIES 256U
typedef enum {
    VM_PAGE_CACHE_FAILED = 0,
    VM_PAGE_CACHE_INVALID,
    VM_PAGE_CACHE_PRIVATE,
    VM_PAGE_CACHE_SHARED
} vm_page_cache_result_t;
typedef struct vm_page_cache_stats {
    uint64_t hits, misses, evictions;
    uint32_t entries, active_mappings;
} vm_page_cache_stats_t;
vm_page_cache_result_t vm_page_cache_acquire_file_page(
    const vm_region_t* region, uintptr_t virtual_page,
    uintptr_t* physical_frame_out);
int vm_page_cache_release(uintptr_t physical_frame);
uint32_t vm_page_cache_reclaim(uint32_t max_pages);
void vm_page_cache_get_stats(vm_page_cache_stats_t* out);
#endif
