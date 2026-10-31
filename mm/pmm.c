/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "pmm.h"
#include "ktrace.h"
#include "spinlock.h"

#include <stdint.h>
#include <stddef.h>

/*
 * 4 GiB / 4 KiB = 1,048,576 physical frames. One bit per frame uses 128 KiB.
 * The bitmaps live in BSS and are therefore covered by the kernel image range
 * reserved below.
 */
static uint32_t pmm_bitmap[PMM_BITMAP_WORDS]
    __attribute__((aligned(4096)));

static uint32_t pmm_reserved_bitmap[PMM_BITMAP_WORDS]
    __attribute__((aligned(4096)));

static uint32_t total_blocks;
static uint32_t free_blocks;
/* Next-fit hint avoids rescanning allocated low memory on every allocation. */
static uint32_t allocation_hint;
static int initialized;
static spinlock_t pmm_lock = SPINLOCK_INITIALIZER;

static inline void bitmap_set(uint32_t* bitmap, uint32_t bit)
{
    bitmap[bit / 32U] |= (1U << (bit % 32U));
}

static inline void bitmap_clear(uint32_t* bitmap, uint32_t bit)
{
    bitmap[bit / 32U] &= ~(1U << (bit % 32U));
}

static inline int bitmap_test(const uint32_t* bitmap, uint32_t bit)
{
    return (bitmap[bit / 32U] & (1U << (bit % 32U))) != 0;
}

static int memory_region_end(
    const vanta_memory_region_t* region,
    uint64_t* end_address
)
{
    if (!region || !end_address ||
        region->length > UINT64_MAX - region->base)
        return 0;

    *end_address = region->base + region->length;
    return 1;
}

static void pmm_reserve_range(uint64_t start, uint64_t end_address)
{
    if (!initialized ||
        start >= PMM_MAX_PHYSICAL_ADDRESS ||
        end_address <= start)
        return;

    if (end_address > PMM_MAX_PHYSICAL_ADDRESS)
        end_address = PMM_MAX_PHYSICAL_ADDRESS;

    uint32_t first = (uint32_t)(start / PAGE_SIZE);
    uint32_t last =
        (uint32_t)((end_address + PAGE_SIZE - 1U) / PAGE_SIZE);

    if (last > total_blocks)
        last = total_blocks;

    for (uint32_t block = first; block < last; block++)
    {
        if (!bitmap_test(pmm_bitmap, block))
        {
            bitmap_set(pmm_bitmap, block);
            if (free_blocks > 0)
                free_blocks--;
        }

        bitmap_set(pmm_reserved_bitmap, block);
    }
}

static void pmm_make_usable_range(uint64_t start, uint64_t end_address)
{
    if (!initialized ||
        start >= PMM_MAX_PHYSICAL_ADDRESS ||
        end_address <= start)
        return;

    if (end_address > PMM_MAX_PHYSICAL_ADDRESS)
        end_address = PMM_MAX_PHYSICAL_ADDRESS;

    /* Only fully covered pages are released to the allocator. */
    uint32_t first =
        (uint32_t)((start + PAGE_SIZE - 1U) / PAGE_SIZE);
    uint32_t last = (uint32_t)(end_address / PAGE_SIZE);

    if (first > total_blocks)
        first = total_blocks;
    if (last > total_blocks)
        last = total_blocks;

    for (uint32_t block = first; block < last; block++)
    {
        if (bitmap_test(pmm_bitmap, block))
        {
            bitmap_clear(pmm_bitmap, block);
            bitmap_clear(pmm_reserved_bitmap, block);
            free_blocks++;
        }
    }
}

static int is_releasable_type(uint32_t type)
{
    return type == VANTA_MEMORY_USABLE ||
           type == VANTA_MEMORY_BOOTLOADER_RECLAIMABLE;
}

void pmm_initialize(const vanta_boot_info_t* boot_info)
{
    spinlock_init(&pmm_lock);
    initialized = 0;
    total_blocks = 0;
    free_blocks = 0;
    allocation_hint = 0U;

    for (uint32_t i = 0; i < PMM_BITMAP_WORDS; i++)
    {
        pmm_bitmap[i] = 0xFFFFFFFFU;
        pmm_reserved_bitmap[i] = 0xFFFFFFFFU;
    }

    if (!boot_info ||
        boot_info->magic != VANTA_BOOT_INFO_MAGIC ||
        boot_info->version != VANTA_BOOT_INFO_VERSION ||
        boot_info->struct_size < sizeof(*boot_info) ||
        boot_info->memory_region_count >
            VANTA_BOOT_INFO_MAX_MEMORY_REGIONS ||
        boot_info->kernel_image_end <= boot_info->kernel_image_start)
        return;

    const int use_memory_map =
        (boot_info->flags & VANTA_BOOT_FLAG_MEMORY_MAP_VALID) != 0 &&
        boot_info->memory_region_count > 0U;

    uint64_t highest_addr = 0;

    if (use_memory_map)
    {
        for (uint32_t i = 0; i < boot_info->memory_region_count; i++)
        {
            const vanta_memory_region_t* region =
                &boot_info->memory_regions[i];
            uint64_t region_end;

            if (!memory_region_end(region, &region_end))
                return;

            if (is_releasable_type(region->type) &&
                region_end > region->base &&
                region->base < PMM_MAX_PHYSICAL_ADDRESS)
            {
                uint64_t top = region_end;
                if (top > PMM_MAX_PHYSICAL_ADDRESS)
                    top = PMM_MAX_PHYSICAL_ADDRESS;
                if (top > highest_addr)
                    highest_addr = top;
            }
        }
    }
    else if (boot_info->source == VANTA_BOOT_SOURCE_MULTIBOOT1 &&
             (boot_info->flags & VANTA_BOOT_FLAG_LEGACY_INFO_PRESENT))
    {
        /* Compatibility fallback for old Multiboot handoffs without a map. */
        highest_addr = 0x100000ULL +
            ((uint64_t)boot_info->legacy_mem_upper_kib * 1024ULL);
        if (highest_addr > PMM_MAX_PHYSICAL_ADDRESS)
            highest_addr = PMM_MAX_PHYSICAL_ADDRESS;
    }
    else
    {
        return;
    }

    if (highest_addr > PMM_MAX_PHYSICAL_ADDRESS)
        highest_addr = PMM_MAX_PHYSICAL_ADDRESS;

    total_blocks =
        (uint32_t)((highest_addr + PAGE_SIZE - 1U) / PAGE_SIZE);
    if (total_blocks > PMM_MAX_BLOCKS)
        total_blocks = PMM_MAX_BLOCKS;
    if (total_blocks == 0)
        return;

    initialized = 1;

    if (use_memory_map)
    {
        /*
         * Release only usable/reclaimable ranges first. Then overlay every
         * non-usable range so reserved entries win if a map overlaps.
         */
        for (uint32_t i = 0; i < boot_info->memory_region_count; i++)
        {
            const vanta_memory_region_t* region =
                &boot_info->memory_regions[i];
            uint64_t region_end;

            if (!memory_region_end(region, &region_end))
            {
                initialized = 0;
                total_blocks = 0;
                free_blocks = 0;
                return;
            }

            if (is_releasable_type(region->type))
                pmm_make_usable_range(region->base, region_end);
        }

        for (uint32_t i = 0; i < boot_info->memory_region_count; i++)
        {
            const vanta_memory_region_t* region =
                &boot_info->memory_regions[i];
            uint64_t region_end;

            if (!memory_region_end(region, &region_end))
            {
                initialized = 0;
                total_blocks = 0;
                free_blocks = 0;
                return;
            }

            if (!is_releasable_type(region->type))
                pmm_reserve_range(region->base, region_end);
        }
    }
    else
    {
        pmm_make_usable_range(0x100000ULL, highest_addr);
    }

    /* Firmware/legacy low memory is never handed to the page allocator. */
    pmm_reserve_range(0, 0x100000ULL);
    pmm_reserve_range(
        boot_info->kernel_image_start,
        boot_info->kernel_image_end
    );

    if (boot_info->flags & VANTA_BOOT_FLAG_LEGACY_INFO_PRESENT)
    {
        if (boot_info->legacy_info_length <=
            UINT64_MAX - boot_info->legacy_info_address)
        {
            pmm_reserve_range(
                boot_info->legacy_info_address,
                boot_info->legacy_info_address +
                    boot_info->legacy_info_length
            );
        }

        if (boot_info->legacy_mmap_length <=
            UINT64_MAX - boot_info->legacy_mmap_address)
        {
            pmm_reserve_range(
                boot_info->legacy_mmap_address,
                boot_info->legacy_mmap_address +
                    boot_info->legacy_mmap_length
            );
        }
    }

    if ((boot_info->flags & VANTA_BOOT_FLAG_FRAMEBUFFER_VALID) &&
        boot_info->framebuffer.height != 0 &&
        boot_info->framebuffer.pitch != 0)
    {
        uint64_t framebuffer_size =
            (uint64_t)boot_info->framebuffer.pitch *
            boot_info->framebuffer.height;

        if (framebuffer_size <=
            UINT64_MAX - boot_info->framebuffer.address)
        {
            pmm_reserve_range(
                boot_info->framebuffer.address,
                boot_info->framebuffer.address + framebuffer_size
            );
        }
    }

    /* Frames above the reported physical limit remain unavailable. */
    for (uint32_t block = total_blocks;
         block < PMM_MAX_BLOCKS;
         block++)
    {
        bitmap_set(pmm_bitmap, block);
        bitmap_set(pmm_reserved_bitmap, block);
    }

    ktrace_emit(KTRACE_EVENT_PMM_READY, KTRACE_CPU_UNKNOWN,
                total_blocks, free_blocks);
}

/*
 * Find the first free frame in [start, end). The bitmap is scanned a word at
 * a time and the first available bit is selected with a bit-scan instruction.
 * Caller holds pmm_lock.
 */
static int pmm_find_free_block_locked(uint32_t start, uint32_t end,
                                      uint32_t* block_out)
{
    if (!block_out || start >= end || end > total_blocks)
        return 0;

    uint32_t first_word = start / 32U;
    uint32_t last_word = (end - 1U) / 32U;
    for (uint32_t word = first_word; word <= last_word; ++word)
    {
        uint32_t low_bit = word == first_word ? start % 32U : 0U;
        uint32_t high_bit = word == last_word ?
            ((end - 1U) % 32U) + 1U : 32U;
        uint32_t mask = UINT32_MAX << low_bit;
        if (high_bit < 32U)
            mask &= (1U << high_bit) - 1U;

        uint32_t available = ~(pmm_bitmap[word] |
                               pmm_reserved_bitmap[word]) & mask;
        if (available != 0U)
        {
            *block_out = word * 32U +
                         (uint32_t)__builtin_ctz(available);
            return 1;
        }
    }
    return 0;
}

void* pmm_alloc_block_below(uint64_t max_address_exclusive)
{
    void *result = NULL;
    uint32_t limit_blocks;
    uint32_t start;
    uint32_t block;
    int found = 0;

    if (max_address_exclusive > PMM_MAX_PHYSICAL_ADDRESS)
        max_address_exclusive = PMM_MAX_PHYSICAL_ADDRESS;
    limit_blocks = (uint32_t)(max_address_exclusive / PAGE_SIZE);
    if (limit_blocks == 0U)
        return NULL;

    spinlock_lock(&pmm_lock);
    if (!initialized || total_blocks == 0U)
    {
        spinlock_unlock(&pmm_lock);
        return NULL;
    }
    if (limit_blocks > total_blocks)
        limit_blocks = total_blocks;

    /*
     * Respect the caller's address ceiling while retaining next-fit behavior.
     * A page is eligible only when its entire 4 KiB extent fits below the
     * exclusive limit, not merely when its first byte does.
     */
    start = allocation_hint < limit_blocks ? allocation_hint : 0U;
    found = pmm_find_free_block_locked(start, limit_blocks, &block);
    if (!found && start != 0U)
        found = pmm_find_free_block_locked(0U, start, &block);

    if (found)
    {
        bitmap_set(pmm_bitmap, block);
        if (free_blocks > 0U)
            free_blocks--;
        allocation_hint = block + 1U;
        if (allocation_hint >= total_blocks)
            allocation_hint = 0U;
        result = (void *)(uintptr_t)((uint64_t)block * PAGE_SIZE);
    }

    uint32_t free_snapshot = free_blocks;
    spinlock_unlock(&pmm_lock);
    if (!found)
        ktrace_emit(KTRACE_EVENT_PMM_ALLOC_FAILURE, KTRACE_CPU_UNKNOWN,
                    limit_blocks, free_snapshot);
    return result;
}

void* pmm_alloc_block(void)
{
    return pmm_alloc_block_below(PMM_MAX_PHYSICAL_ADDRESS);
}

int pmm_alloc_contiguous_blocks_below(uint32_t count,
                                      uint64_t max_address_exclusive,
                                      void **base_address)
{
    uint32_t run_start = 0U;
    uint32_t run_length = 0U;
    uint32_t limit_blocks;

    if (base_address == NULL)
        return 0;
    *base_address = NULL;
    if (count == 0U)
        return 0;
    if (max_address_exclusive > PMM_MAX_PHYSICAL_ADDRESS)
        max_address_exclusive = PMM_MAX_PHYSICAL_ADDRESS;
    limit_blocks = (uint32_t)(max_address_exclusive / PAGE_SIZE);
    if (limit_blocks < count)
        return 0;

    spinlock_lock(&pmm_lock);
    if (!initialized || count > total_blocks || count > free_blocks)
    {
        spinlock_unlock(&pmm_lock);
        return 0;
    }
    if (limit_blocks > total_blocks)
        limit_blocks = total_blocks;
    if (count > limit_blocks)
    {
        spinlock_unlock(&pmm_lock);
        return 0;
    }

    /* Search the complete eligible physical range; next-fit hints can split
       an otherwise valid contiguous run and are intentionally not used here. */
    for (uint32_t block = 0U; block <= limit_blocks - count; ++block)
    {
        if (!bitmap_test(pmm_bitmap, block) &&
            !bitmap_test(pmm_reserved_bitmap, block))
        {
            if (run_length == 0U)
                run_start = block;
            ++run_length;
            if (run_length == count)
            {
                for (uint32_t i = 0U; i < count; ++i)
                    bitmap_set(pmm_bitmap, run_start + i);
                free_blocks -= count;
                *base_address = (void *)(uintptr_t)(
                    (uint64_t)run_start * PAGE_SIZE);
                spinlock_unlock(&pmm_lock);
                return 1;
            }
        }
        else
        {
            run_length = 0U;
        }
    }

    spinlock_unlock(&pmm_lock);
    return 0;
}

int pmm_alloc_contiguous_blocks(uint32_t count, void **base_address)
{
    return pmm_alloc_contiguous_blocks_below(
        count, PMM_MAX_PHYSICAL_ADDRESS, base_address);
}

void pmm_free_block(void* ptr)
{
    if (!ptr)
        return;

    uintptr_t address = (uintptr_t)ptr;
    if ((address % PAGE_SIZE) != 0 ||
        address >= PMM_MAX_PHYSICAL_ADDRESS)
        return;

    spinlock_lock(&pmm_lock);
    if (!initialized)
    {
        spinlock_unlock(&pmm_lock);
        return;
    }

    uint32_t block = (uint32_t)(address / PAGE_SIZE);

    if (block >= total_blocks ||
        bitmap_test(pmm_reserved_bitmap, block) ||
        !bitmap_test(pmm_bitmap, block))
    {
        spinlock_unlock(&pmm_lock);
        return;
    }

    bitmap_clear(pmm_bitmap, block);
    ++free_blocks;
    spinlock_unlock(&pmm_lock);
}

uint32_t pmm_get_total_blocks(void)
{
    uint32_t result;
    spinlock_lock(&pmm_lock);
    result = total_blocks;
    spinlock_unlock(&pmm_lock);
    return result;
}

uint32_t pmm_get_used_blocks(void)
{
    uint32_t result;
    spinlock_lock(&pmm_lock);
    result = total_blocks - free_blocks;
    spinlock_unlock(&pmm_lock);
    return result;
}

uint32_t pmm_get_free_blocks(void)
{
    uint32_t result;
    spinlock_lock(&pmm_lock);
    result = free_blocks;
    spinlock_unlock(&pmm_lock);
    return result;
}

int pmm_is_initialized(void)
{
    return initialized != 0;
}
