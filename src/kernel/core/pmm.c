#include "pmm.h"

#include <stdint.h>
#include <stddef.h>

/*
 * The PMM metadata lives in the kernel's BSS instead of being placed
 * immediately after the linked kernel image. This makes its location
 * independent of the kernel size and prevents it from overlapping
 * Multiboot data or other early allocations.
 *
 * 4 GiB / 4 KiB = 1,048,576 physical frames.
 * One bit per frame therefore needs 128 KiB.
 */
static uint32_t pmm_bitmap[PMM_BITMAP_WORDS]
    __attribute__((aligned(4096)));

static uint32_t pmm_reserved_bitmap[PMM_BITMAP_WORDS]
    __attribute__((aligned(4096)));

static uint32_t total_blocks;
static uint32_t free_blocks;
static int initialized;

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

void pmm_initialize(multiboot_info_t* mbd)
{
    initialized = 0;
    total_blocks = 0;
    free_blocks = 0;

    for (uint32_t i = 0; i < PMM_BITMAP_WORDS; i++)
    {
        pmm_bitmap[i] = 0xFFFFFFFFU;
        pmm_reserved_bitmap[i] = 0xFFFFFFFFU;
    }

    if (!mbd)
        return;

    uint64_t highest_addr = 0;
    uint64_t mmap_start = 0;
    uint64_t mmap_end_address = 0;
    int use_mmap = 0;

    /*
     * Multiboot 1 normally supplies the full firmware memory map, but
     * some boot paths only provide the legacy mem_upper field. Do not
     * leave the PMM completely offline just because the mmap handoff is
     * unavailable or malformed.
     */
    if ((mbd->flags & MULTIBOOT_INFO_MEM_MAP) &&
        mbd->mmap_addr != 0 &&
        mbd->mmap_length >= sizeof(uint32_t))
    {
        mmap_start = mbd->mmap_addr;
        mmap_end_address =
            mmap_start + mbd->mmap_length;

        if (mmap_end_address > mmap_start &&
            mmap_end_address <= PMM_MAX_PHYSICAL_ADDRESS)
        {
            multiboot_memory_map_t* mmap =
                (multiboot_memory_map_t*)(uintptr_t)mmap_start;

            use_mmap = 1;

            while ((uint64_t)(uintptr_t)mmap < mmap_end_address)
            {
                uint64_t entry_start =
                    (uint64_t)(uintptr_t)mmap;
                uint64_t entry_end =
                    entry_start +
                    mmap->size +
                    sizeof(mmap->size);

                if (mmap->size < 20 ||
                    entry_end <= entry_start ||
                    entry_end > mmap_end_address)
                {
                    use_mmap = 0;
                    break;
                }

                if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE)
                {
                    uint64_t top = mmap->addr + mmap->len;

                    if (top > highest_addr)
                        highest_addr = top;
                }

                mmap =
                    (multiboot_memory_map_t*)(uintptr_t)entry_end;
            }
        }
    }

    /*
     * Legacy Multiboot memory information reports memory above 1 MiB
     * in KiB. It is less precise than the memory map, but it is a safe
     * fallback for early boot and is enough to bring the PMM online.
     */
    if (!use_mmap)
    {
        highest_addr =
            0x100000ULL +
            ((uint64_t)mbd->mem_upper * 1024ULL);

        if (highest_addr > PMM_MAX_PHYSICAL_ADDRESS)
            highest_addr = PMM_MAX_PHYSICAL_ADDRESS;
    }
    else if (highest_addr > PMM_MAX_PHYSICAL_ADDRESS)
    {
        highest_addr = PMM_MAX_PHYSICAL_ADDRESS;
    }

    total_blocks =
        (uint32_t)((highest_addr + PAGE_SIZE - 1U) / PAGE_SIZE);

    if (total_blocks > PMM_MAX_BLOCKS)
        total_blocks = PMM_MAX_BLOCKS;

    if (total_blocks == 0)
        return;

    initialized = 1;

    /*
     * Start with every frame unavailable, then release only frames
     * explicitly reported by the firmware as usable RAM.
     */
    mmap = (multiboot_memory_map_t*)(uintptr_t)mmap_start;

    while ((uint64_t)(uintptr_t)mmap < mmap_end_address)
    {
        uint64_t entry_start = (uint64_t)(uintptr_t)mmap;
        uint64_t entry_end =
            entry_start + mmap->size + sizeof(mmap->size);

        if (mmap->size < 20 ||
            entry_end <= entry_start ||
            entry_end > mmap_end_address)
            return;

        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE)
        {
            uint64_t region_start = mmap->addr;
            uint64_t region_end = mmap->addr + mmap->len;

            if (region_start < PMM_MAX_PHYSICAL_ADDRESS &&
                region_end > region_start)
            {
                if (region_end > PMM_MAX_PHYSICAL_ADDRESS)
                    region_end = PMM_MAX_PHYSICAL_ADDRESS;

                uint32_t start_block =
                    (uint32_t)((region_start + PAGE_SIZE - 1U) /
                               PAGE_SIZE);

                uint32_t end_block =
                    (uint32_t)(region_end / PAGE_SIZE);

                if (end_block > total_blocks)
                    end_block = total_blocks;

                for (uint32_t block = start_block;
                     block < end_block;
                     block++)
                {
                    if (bitmap_test(pmm_bitmap, block))
                    {
                        bitmap_clear(pmm_bitmap, block);
                        bitmap_clear(pmm_reserved_bitmap, block);
                        free_blocks++;
                    }
                }
            }
        }

        mmap = (multiboot_memory_map_t*)(uintptr_t)entry_end;
    }

    /*
     * Reserve the first MiB for firmware/boot structures and keep
     * the kernel image itself unavailable to the allocator.
     *
     * The linker-provided 'end' symbol is safe here because the PMM
     * bitmap itself is already part of the kernel image/BSS.
     */
    extern uint32_t end;

    pmm_reserve_range(0, 0x100000);
    pmm_reserve_range(0x100000, (uintptr_t)&end);

    /*
     * Reserve the Multiboot structures we directly reference.
     */
    pmm_reserve_range(
        (uint64_t)(uintptr_t)mbd,
        (uint64_t)(uintptr_t)mbd + sizeof(multiboot_info_t)
    );

    pmm_reserve_range(
        mbd->mmap_addr,
        (uint64_t)mbd->mmap_addr + mbd->mmap_length
    );

    if (mbd->flags & MULTIBOOT_INFO_FRAMEBUFFER)
    {
        uint64_t framebuffer_end =
            mbd->framebuffer_addr +
            (uint64_t)mbd->framebuffer_pitch *
            mbd->framebuffer_height;

        pmm_reserve_range(
            mbd->framebuffer_addr,
            framebuffer_end
        );
    }

    /*
     * Frames above the reported physical limit are permanently
     * unavailable because they are outside the usable address space.
     */
    for (uint32_t block = total_blocks;
         block < PMM_MAX_BLOCKS;
         block++)
    {
        bitmap_set(pmm_bitmap, block);
        bitmap_set(pmm_reserved_bitmap, block);
    }
}

void* pmm_alloc_block(void)
{
    if (!initialized || total_blocks == 0)
        return NULL;

    for (uint32_t word = 0;
         word < (total_blocks + 31U) / 32U;
         word++)
    {
        uint32_t bits = pmm_bitmap[word];

        if (bits == 0xFFFFFFFFU)
            continue;

        for (uint32_t bit = 0; bit < 32U; bit++)
        {
            uint32_t block = word * 32U + bit;

            if (block >= total_blocks)
                break;

            if (!bitmap_test(pmm_bitmap, block) &&
                !bitmap_test(pmm_reserved_bitmap, block))
            {
                bitmap_set(pmm_bitmap, block);

                if (free_blocks > 0)
                    free_blocks--;

                return (void*)(uintptr_t)
                    (block * PAGE_SIZE);
            }
        }
    }

    return NULL;
}

void pmm_free_block(void* ptr)
{
    if (!initialized || !ptr)
        return;

    uintptr_t address = (uintptr_t)ptr;

    if ((address % PAGE_SIZE) != 0 ||
        address >= PMM_MAX_PHYSICAL_ADDRESS)
        return;

    uint32_t block = (uint32_t)(address / PAGE_SIZE);

    if (block >= total_blocks ||
        bitmap_test(pmm_reserved_bitmap, block) ||
        !bitmap_test(pmm_bitmap, block))
        return;

    bitmap_clear(pmm_bitmap, block);
    free_blocks++;
}

uint32_t pmm_get_total_blocks(void)
{
    return total_blocks;
}

uint32_t pmm_get_used_blocks(void)
{
    return total_blocks - free_blocks;
}

uint32_t pmm_get_free_blocks(void)
{
    return free_blocks;
}

int pmm_is_initialized(void)
{
    return initialized != 0;
}
