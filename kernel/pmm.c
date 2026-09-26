#include "pmm.h"

#include <stdint.h>
#include <stddef.h>

extern uint32_t end;

static uint32_t* pmm_bitmap;
static uint32_t total_blocks;
static uint32_t bitmap_size;

static inline void bitmap_set(uint32_t bit)
{
    pmm_bitmap[bit / 32] |= (1U << (bit % 32));
}

static inline void bitmap_clear(uint32_t bit)
{
    pmm_bitmap[bit / 32] &= ~(1U << (bit % 32));
}

static inline int bitmap_test(uint32_t bit)
{
    return (pmm_bitmap[bit / 32] & (1U << (bit % 32))) != 0;
}

void pmm_initialize(multiboot_info_t* mbd)
{
    if (!(mbd->flags & MULTIBOOT_INFO_MEM_MAP))
    {
        pmm_bitmap = NULL;
        total_blocks = 0;
        bitmap_size = 0;
        return;
    }

    uint64_t highest_addr = 0;

    multiboot_memory_map_t* mmap =
        (multiboot_memory_map_t*)mbd->mmap_addr;

    uint32_t mmap_end =
        mbd->mmap_addr + mbd->mmap_length;

    while ((uint32_t)mmap < mmap_end)
    {
        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE)
        {
            uint64_t top = mmap->addr + mmap->len;

            if (top > highest_addr)
                highest_addr = top;
        }

        mmap = (multiboot_memory_map_t*)
            ((uint32_t)mmap + mmap->size + sizeof(mmap->size));
    }

    if (highest_addr > 0x100000000ULL)
        highest_addr = 0x100000000ULL;

    total_blocks =
        (uint32_t)((highest_addr + PAGE_SIZE - 1) / PAGE_SIZE);

    bitmap_size =
        (total_blocks + 31) / 32;

    uintptr_t bitmap_address =
        ((uintptr_t)&end + 3) & ~(uintptr_t)3;

    pmm_bitmap = (uint32_t*)bitmap_address;

    for (uint32_t i = 0; i < bitmap_size; i++)
    {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }

    mmap = (multiboot_memory_map_t*)mbd->mmap_addr;

    while ((uint32_t)mmap < mmap_end)
    {
        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE)
        {
            uint64_t region_start = mmap->addr;
            uint64_t region_end = mmap->addr + mmap->len;

            if (region_start < 0x100000000ULL)
            {
                if (region_end > 0x100000000ULL)
                    region_end = 0x100000000ULL;

                uint32_t start_block =
                    (uint32_t)(region_start / PAGE_SIZE);

                uint32_t end_block =
                    (uint32_t)(region_end / PAGE_SIZE);

                for (uint32_t block = start_block;
                     block < end_block && block < total_blocks;
                     block++)
                {
                    bitmap_clear(block);
                }
            }
        }

        mmap = (multiboot_memory_map_t*)
            ((uint32_t)mmap + mmap->size + sizeof(mmap->size));
    }

    uintptr_t bitmap_end =
        bitmap_address +
        (bitmap_size * sizeof(uint32_t));

    uint32_t reserved_end_block =
        (uint32_t)((bitmap_end + PAGE_SIZE - 1) / PAGE_SIZE);

    uint32_t reserved_end = reserved_end_block;

    if (reserved_end < 256)
        reserved_end = 256;

    for (uint32_t block = 0;
         block < reserved_end && block < total_blocks;
         block++)
    {
        bitmap_set(block);
    }

    for (uint32_t block = total_blocks;
         block < bitmap_size * 32;
         block++)
    {
        bitmap_set(block);
    }
}

void* pmm_alloc_block(void)
{
    if (!pmm_bitmap || total_blocks == 0)
        return NULL;

    for (uint32_t i = 0; i < bitmap_size; i++)
    {
        if (pmm_bitmap[i] == 0xFFFFFFFF)
            continue;

        for (uint32_t j = 0; j < 32; j++)
        {
            uint32_t block = i * 32 + j;

            if (block >= total_blocks)
                break;

            if (!bitmap_test(block))
            {
                bitmap_set(block);

                return (void*)(uintptr_t)
                    (block * PAGE_SIZE);
            }
        }
    }

    return NULL;
}

void pmm_free_block(void* ptr)
{
    if (!pmm_bitmap || !ptr)
        return;

    uintptr_t address = (uintptr_t)ptr;

    if (address % PAGE_SIZE != 0)
        return;

    uint32_t block =
        (uint32_t)(address / PAGE_SIZE);

    if (block >= total_blocks)
        return;

    bitmap_clear(block);
}