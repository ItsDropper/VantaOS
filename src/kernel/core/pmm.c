#include "pmm.h"

#include <stdint.h>
#include <stddef.h>

extern uint32_t end;

#define MULTIBOOT_INFO_CMDLINE       (1 << 2)
#define MULTIBOOT_INFO_MODS          (1 << 3)
#define MULTIBOOT_INFO_BOOT_LOADER   (1 << 9)
#define MULTIBOOT_INFO_VBE           (1 << 11)

typedef struct
{
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
} __attribute__((packed)) multiboot_module_t;

static uint32_t* pmm_bitmap;
static uint32_t* pmm_reserved_bitmap;
static uint32_t total_blocks;
static uint32_t bitmap_size;
static uint32_t free_blocks;

static inline void bitmap_set(uint32_t* bitmap, uint32_t bit)
{
    bitmap[bit / 32] |= (1U << (bit % 32));
}

static inline void bitmap_clear(uint32_t* bitmap, uint32_t bit)
{
    bitmap[bit / 32] &= ~(1U << (bit % 32));
}

static inline int bitmap_test(const uint32_t* bitmap, uint32_t bit)
{
    return (bitmap[bit / 32] & (1U << (bit % 32))) != 0;
}

static void pmm_reserve_range(uint64_t start, uint64_t end_address)
{
    if (!pmm_bitmap ||
        !pmm_reserved_bitmap ||
        start >= 0x100000000ULL ||
        end_address <= start)
        return;

    if (end_address > 0x100000000ULL)
        end_address = 0x100000000ULL;

    uint32_t first = (uint32_t)(start / PAGE_SIZE);
    uint32_t last =
        (uint32_t)((end_address + PAGE_SIZE - 1) / PAGE_SIZE);

    if (first >= total_blocks)
        return;

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

static void pmm_reserve_cstring(uint32_t address)
{
    if (address == 0)
        return;

    uint32_t length = 0;

    while (length < 4096 && *((const char*)(uintptr_t)address + length))
        length++;

    if (length < 4096)
        length++;

    pmm_reserve_range(
        address,
        (uint64_t)address + length
    );
}

void pmm_initialize(multiboot_info_t* mbd)
{
    pmm_bitmap = NULL;
    pmm_reserved_bitmap = NULL;
    total_blocks = 0;
    bitmap_size = 0;
    free_blocks = 0;

    if (!mbd ||
        !(mbd->flags & MULTIBOOT_INFO_MEM_MAP) ||
        mbd->mmap_addr == 0 ||
        mbd->mmap_length < sizeof(uint32_t))
        return;

    uint64_t mmap_start = mbd->mmap_addr;
    uint64_t mmap_end_address =
        mmap_start + mbd->mmap_length;

    if (mmap_end_address > 0x100000000ULL ||
        mmap_end_address <= mmap_start)
        return;

    uint64_t highest_addr = 0;
    multiboot_memory_map_t* mmap =
        (multiboot_memory_map_t*)(uintptr_t)mmap_start;

    while ((uint64_t)(uintptr_t)mmap < mmap_end_address)
    {
        uint64_t entry_start = (uint64_t)(uintptr_t)mmap;
        uint64_t entry_end =
            entry_start + mmap->size + sizeof(mmap->size);

        if (mmap->size < 20 ||
            entry_end > mmap_end_address ||
            entry_end <= entry_start)
            return;

        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE)
        {
            uint64_t top = mmap->addr + mmap->len;

            if (top > highest_addr)
                highest_addr = top;
        }

        mmap = (multiboot_memory_map_t*)(uintptr_t)entry_end;
    }

    if (highest_addr > 0x100000000ULL)
        highest_addr = 0x100000000ULL;

    total_blocks =
        (uint32_t)((highest_addr + PAGE_SIZE - 1) / PAGE_SIZE);

    if (total_blocks == 0)
    {
        total_blocks = 0;
        return;
    }

    bitmap_size = (total_blocks + 31) / 32;

    uintptr_t bitmap_address =
        ((uintptr_t)&end + PAGE_SIZE - 1) &
        ~(uintptr_t)(PAGE_SIZE - 1);

    uintptr_t reserved_bitmap_address =
        bitmap_address +
        (uintptr_t)bitmap_size * sizeof(uint32_t);

    uintptr_t bitmap_end =
        reserved_bitmap_address +
        (uintptr_t)bitmap_size * sizeof(uint32_t);

    if (bitmap_end <= bitmap_address ||
        (uint64_t)bitmap_end > highest_addr)
    {
        total_blocks = 0;
        bitmap_size = 0;
        return;
    }

    pmm_bitmap = (uint32_t*)bitmap_address;
    pmm_reserved_bitmap =
        (uint32_t*)reserved_bitmap_address;

    for (uint32_t i = 0; i < bitmap_size; i++)
    {
        pmm_bitmap[i] = 0xFFFFFFFF;
        pmm_reserved_bitmap[i] = 0xFFFFFFFF;
    }

    mmap = (multiboot_memory_map_t*)(uintptr_t)mmap_start;

    while ((uint64_t)(uintptr_t)mmap < mmap_end_address)
    {
        uint64_t entry_start = (uint64_t)(uintptr_t)mmap;
        uint64_t entry_end =
            entry_start + mmap->size + sizeof(mmap->size);

        if (mmap->size < 20 ||
            entry_end > mmap_end_address ||
            entry_end <= entry_start)
            return;

        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE)
        {
            uint64_t region_start = mmap->addr;
            uint64_t region_end = mmap->addr + mmap->len;

            if (region_start < 0x100000000ULL &&
                region_end > region_start)
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

    /* Low memory is never available to the general allocator. */
    pmm_reserve_range(0, 0x100000);

    /* Kernel image and PMM metadata are permanently reserved. */
    pmm_reserve_range(
        0x100000,
        (uint64_t)(uintptr_t)&end
    );

    pmm_reserve_range(
        bitmap_address,
        bitmap_end
    );

    /* Multiboot structures must remain valid for the kernel. */
    pmm_reserve_range(
        (uint64_t)(uintptr_t)mbd,
        (uint64_t)(uintptr_t)mbd + sizeof(multiboot_info_t)
    );

    pmm_reserve_range(
        mbd->mmap_addr,
        (uint64_t)mbd->mmap_addr + mbd->mmap_length
    );

    if (mbd->flags & MULTIBOOT_INFO_CMDLINE)
        pmm_reserve_cstring(mbd->cmdline);

    if (mbd->flags & MULTIBOOT_INFO_BOOT_LOADER)
        pmm_reserve_cstring(mbd->boot_loader_name);

    if (mbd->flags & MULTIBOOT_INFO_MODS)
    {
        uint64_t modules_size =
            (uint64_t)mbd->mods_count *
            sizeof(multiboot_module_t);

        pmm_reserve_range(
            mbd->mods_addr,
            (uint64_t)mbd->mods_addr + modules_size
        );

        if (modules_size <= 0x100000 &&
            mbd->mods_addr != 0)
        {
            multiboot_module_t* modules =
                (multiboot_module_t*)(uintptr_t)mbd->mods_addr;

            for (uint32_t i = 0; i < mbd->mods_count; i++)
            {
                if (modules[i].mod_end > modules[i].mod_start)
                {
                    pmm_reserve_range(
                        modules[i].mod_start,
                        modules[i].mod_end
                    );

                    pmm_reserve_cstring(modules[i].string);
                }
            }
        }
    }

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

    if (mbd->flags & MULTIBOOT_INFO_VBE)
    {
        pmm_reserve_range(
            mbd->vbe_control_info,
            (uint64_t)mbd->vbe_control_info + 512
        );

        pmm_reserve_range(
            mbd->vbe_mode_info,
            (uint64_t)mbd->vbe_mode_info + 256
        );
    }

    for (uint32_t block = total_blocks;
         block < bitmap_size * 32;
         block++)
    {
        bitmap_set(pmm_bitmap, block);
        bitmap_set(pmm_reserved_bitmap, block);
    }
}

void* pmm_alloc_block(void)
{
    if (!pmm_bitmap ||
        !pmm_reserved_bitmap ||
        total_blocks == 0)
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

            if (!bitmap_test(pmm_bitmap, block) &&
                !bitmap_test(pmm_reserved_bitmap, block))
            {
                bitmap_set(pmm_bitmap, block);

                if (free_blocks > 0)
                    free_blocks--;

                return (void*)(uintptr_t)(block * PAGE_SIZE);
            }
        }
    }

    return NULL;
}

void pmm_free_block(void* ptr)
{
    if (!pmm_bitmap ||
        !pmm_reserved_bitmap ||
        !ptr)
        return;

    uintptr_t address = (uintptr_t)ptr;

    if ((address % PAGE_SIZE) != 0)
        return;

    uint32_t block =
        (uint32_t)(address / PAGE_SIZE);

    if (block >= total_blocks)
        return;

    if (!bitmap_test(pmm_bitmap, block) ||
        bitmap_test(pmm_reserved_bitmap, block))
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
    return pmm_bitmap != NULL &&
           pmm_reserved_bitmap != NULL &&
           total_blocks != 0;
}
