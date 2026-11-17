/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 */
#include "vm_page_cache.h"
#include "pmm.h"
#include "heap.h"
#include "spinlock.h"
#include <stddef.h>
#include <stdint.h>

#define VM_PAGE_CACHE_PHYSICAL_LIMIT 0x40000000ULL

typedef struct vm_page_cache_entry {
    struct vm_page_cache_entry* next;
    uintptr_t frame;
    uint64_t hash;
    uint32_t references;
} vm_page_cache_entry_t;

typedef struct vm_page_cache_view {
    const uint8_t* source;
    size_t destination_offset;
    size_t copy_bytes;
} vm_page_cache_view_t;

static spinlock_t cache_lock = SPINLOCK_INITIALIZER;
static vm_page_cache_entry_t* cache_head;
static uint32_t cache_count;
static volatile uint64_t cache_hits, cache_misses, cache_evictions;
static volatile uint32_t cache_active_mappings;

static int cache_build_view(const vm_region_t* region, uintptr_t page,
                            vm_page_cache_view_t* view)
{
    if (!region || !view || region->backing != VM_BACKING_FILE ||
        !region->file_data || page < region->start || page >= region->end ||
        (page & (PAGE_SIZE - 1U)) != 0U ||
        region->file_prefix >= PAGE_SIZE ||
        region->file_bytes > UINT64_MAX - region->file_prefix)
        return 0;

    const uint64_t page_offset = (uint64_t)(page - region->start);
    const uint64_t file_end = (uint64_t)region->file_prefix +
                              region->file_bytes;
    if (file_end > (uint64_t)(region->end - region->start) ||
        page_offset >= file_end)
        return 0;

    const uint64_t copy_start = page_offset > region->file_prefix ?
                                page_offset : region->file_prefix;
    const uint64_t page_end = page_offset + PAGE_SIZE;
    const uint64_t copy_end = file_end < page_end ? file_end : page_end;
    const uint64_t relative_source = copy_start - region->file_prefix;
    if (relative_source > UINT64_MAX - region->file_offset ||
        relative_source > region->file_bytes ||
        copy_end < copy_start)
        return 0;

    view->source = region->file_data + region->file_offset + relative_source;
    view->destination_offset = (size_t)(copy_start - page_offset);
    view->copy_bytes = (size_t)(copy_end - copy_start);
    return 1;
}

static uint8_t cache_view_byte(const vm_page_cache_view_t* view, size_t offset)
{
    if (offset < view->destination_offset ||
        offset - view->destination_offset >= view->copy_bytes)
        return 0U;
    return view->source[offset - view->destination_offset];
}

static uint64_t cache_hash_view(const vm_page_cache_view_t* view)
{
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0U; i < PAGE_SIZE; i++) {
        hash ^= cache_view_byte(view, i);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static int cache_frame_matches_view(uintptr_t frame,
                                    const vm_page_cache_view_t* view)
{
    const uint8_t* bytes = (const uint8_t*)frame;
    for (size_t i = 0U; i < PAGE_SIZE; i++)
        if (bytes[i] != cache_view_byte(view, i))
            return 0;
    return 1;
}

/* Remove the least-recently-used idle entry. Caller holds cache_lock. */
static vm_page_cache_entry_t* cache_evict_idle_locked(void)
{
    vm_page_cache_entry_t** link = &cache_head;
    vm_page_cache_entry_t** victim_link = NULL;
    while (*link) {
        if ((*link)->references == 0U)
            victim_link = link;
        link = &(*link)->next;
    }
    if (!victim_link)
        return NULL;

    vm_page_cache_entry_t* victim = *victim_link;
    *victim_link = victim->next;
    victim->next = NULL;
    cache_count--;
    __atomic_fetch_add(&cache_evictions, 1U, __ATOMIC_RELAXED);
    return victim;
}

/* Lookup, acquire a mapping reference and promote to MRU. Caller holds lock. */
static vm_page_cache_entry_t* cache_find_locked(
    uint64_t hash, const vm_page_cache_view_t* view)
{
    for (vm_page_cache_entry_t* entry = cache_head;
         entry; entry = entry->next) {
        if (entry->hash != hash || entry->references == UINT32_MAX ||
            !cache_frame_matches_view(entry->frame, view))
            continue;

        entry->references++;
        __atomic_fetch_add(&cache_active_mappings, 1U, __ATOMIC_RELAXED);
        __atomic_fetch_add(&cache_hits, 1U, __ATOMIC_RELAXED);
        if (entry != cache_head) {
            vm_page_cache_entry_t** link = &cache_head;
            while (*link && *link != entry)
                link = &(*link)->next;
            if (*link == entry) {
                *link = entry->next;
                entry->next = cache_head;
                cache_head = entry;
            }
        }
        return entry;
    }
    return NULL;
}

static void cache_free_entry(vm_page_cache_entry_t* entry)
{
    if (!entry)
        return;
    pmm_free_block((void*)entry->frame);
    kfree(entry);
}

static void cache_zero_page(void* frame)
{
    volatile uint64_t* words = (volatile uint64_t*)frame;
    for (size_t i = 0U; i < PAGE_SIZE / sizeof(uint64_t); i++)
        words[i] = 0U;
}

uint32_t vm_page_cache_reclaim(uint32_t max_pages)
{
    vm_page_cache_entry_t* reclaimed = NULL;
    uint32_t count = 0U;
    uint64_t flags = spinlock_lock_irqsave(&cache_lock);
    while (count < max_pages) {
        vm_page_cache_entry_t* entry = cache_evict_idle_locked();
        if (!entry)
            break;
        entry->next = reclaimed;
        reclaimed = entry;
        count++;
    }
    spinlock_unlock_irqrestore(&cache_lock, flags);

    while (reclaimed) {
        vm_page_cache_entry_t* next = reclaimed->next;
        cache_free_entry(reclaimed);
        reclaimed = next;
    }
    return count;
}

vm_page_cache_result_t vm_page_cache_acquire_file_page(
    const vm_region_t* region, uintptr_t virtual_page,
    uintptr_t* physical_frame_out)
{
    if (!physical_frame_out || !region ||
        region->backing != VM_BACKING_FILE ||
        (region->prot & VM_PROT_WRITE) != 0U)
        return VM_PAGE_CACHE_INVALID;

    vm_page_cache_view_t view;
    if (!cache_build_view(region, virtual_page, &view))
        return VM_PAGE_CACHE_INVALID;
    const uint64_t hash = cache_hash_view(&view);

    /* A cache hit must not need a spare physical frame. */
    uint64_t flags = spinlock_lock_irqsave(&cache_lock);
    vm_page_cache_entry_t* found = cache_find_locked(hash, &view);
    if (found) {
        *physical_frame_out = found->frame;
        spinlock_unlock_irqrestore(&cache_lock, flags);
        return VM_PAGE_CACHE_SHARED;
    }
    spinlock_unlock_irqrestore(&cache_lock, flags);

    void* candidate = pmm_alloc_block_below(VM_PAGE_CACHE_PHYSICAL_LIMIT);
    if (!candidate) {
        (void)vm_page_cache_reclaim(VM_PAGE_CACHE_MAX_ENTRIES);
        candidate = pmm_alloc_block_below(VM_PAGE_CACHE_PHYSICAL_LIMIT);
    }
    if (!candidate) {
        /* A concurrent fault may have inserted the page while we reclaimed. */
        flags = spinlock_lock_irqsave(&cache_lock);
        found = cache_find_locked(hash, &view);
        if (found) {
            *physical_frame_out = found->frame;
            spinlock_unlock_irqrestore(&cache_lock, flags);
            return VM_PAGE_CACHE_SHARED;
        }
        spinlock_unlock_irqrestore(&cache_lock, flags);
        return VM_PAGE_CACHE_FAILED;
    }
    if (((uintptr_t)candidate & (PAGE_SIZE - 1U)) != 0U ||
        (uintptr_t)candidate >= VM_PAGE_CACHE_PHYSICAL_LIMIT) {
        pmm_free_block(candidate);
        return VM_PAGE_CACHE_FAILED;
    }

    cache_zero_page(candidate);
    if (!vm_region_copy_file_page(region, virtual_page, candidate)) {
        pmm_free_block(candidate);
        return VM_PAGE_CACHE_INVALID;
    }

    vm_page_cache_entry_t* fresh =
        (vm_page_cache_entry_t*)kmalloc(sizeof(*fresh));
    flags = spinlock_lock_irqsave(&cache_lock);

    /* Recheck after allocation so simultaneous faults converge on one frame. */
    found = cache_find_locked(hash, &view);
    if (found) {
        *physical_frame_out = found->frame;
        spinlock_unlock_irqrestore(&cache_lock, flags);
        pmm_free_block(candidate);
        if (fresh)
            kfree(fresh);
        return VM_PAGE_CACHE_SHARED;
    }

    __atomic_fetch_add(&cache_misses, 1U, __ATOMIC_RELAXED);
    if (!fresh) {
        spinlock_unlock_irqrestore(&cache_lock, flags);
        *physical_frame_out = (uintptr_t)candidate;
        return VM_PAGE_CACHE_PRIVATE;
    }

    vm_page_cache_entry_t* evicted = NULL;
    if (cache_count >= VM_PAGE_CACHE_MAX_ENTRIES)
        evicted = cache_evict_idle_locked();
    if (cache_count >= VM_PAGE_CACHE_MAX_ENTRIES) {
        spinlock_unlock_irqrestore(&cache_lock, flags);
        if (evicted)
            cache_free_entry(evicted);
        kfree(fresh);
        *physical_frame_out = (uintptr_t)candidate;
        return VM_PAGE_CACHE_PRIVATE;
    }

    fresh->frame = (uintptr_t)candidate;
    fresh->hash = hash;
    fresh->references = 1U;
    fresh->next = cache_head;
    cache_head = fresh;
    cache_count++;
    __atomic_fetch_add(&cache_active_mappings, 1U, __ATOMIC_RELAXED);
    *physical_frame_out = (uintptr_t)candidate;
    spinlock_unlock_irqrestore(&cache_lock, flags);
    if (evicted)
        cache_free_entry(evicted);
    return VM_PAGE_CACHE_SHARED;
}

int vm_page_cache_release(uintptr_t physical_frame)
{
    int released = 0;
    uint64_t flags = spinlock_lock_irqsave(&cache_lock);
    for (vm_page_cache_entry_t* entry = cache_head;
         entry; entry = entry->next) {
        if (entry->frame != physical_frame)
            continue;
        if (entry->references != 0U) {
            entry->references--;
            __atomic_fetch_sub(&cache_active_mappings, 1U, __ATOMIC_RELAXED);
            released = 1;
        }
        break;
    }
    spinlock_unlock_irqrestore(&cache_lock, flags);
    return released;
}

void vm_page_cache_get_stats(vm_page_cache_stats_t* out)
{
    if (!out)
        return;
    out->hits = __atomic_load_n(&cache_hits, __ATOMIC_RELAXED);
    out->misses = __atomic_load_n(&cache_misses, __ATOMIC_RELAXED);
    out->evictions = __atomic_load_n(&cache_evictions, __ATOMIC_RELAXED);
    out->active_mappings =
        __atomic_load_n(&cache_active_mappings, __ATOMIC_RELAXED);
    uint64_t flags = spinlock_lock_irqsave(&cache_lock);
    out->entries = cache_count;
    spinlock_unlock_irqrestore(&cache_lock, flags);
}
