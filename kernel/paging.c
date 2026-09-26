#include "paging.h"
#include "pmm.h"

#include <stdint.h>

#define PAGE_PRESENT  0x001
#define PAGE_WRITE    0x002

#define PAGE_TABLE_COUNT 4
#define IDENTITY_MAP_SIZE (PAGE_TABLE_COUNT * 1024 * PAGE_SIZE)

static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t page_tables[PAGE_TABLE_COUNT][1024]
    __attribute__((aligned(4096)));

static inline void load_page_directory(uint32_t* directory)
{
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(directory)
        : "memory"
    );
}

static inline void enable_paging(void)
{
    uint32_t cr0;

    __asm__ volatile (
        "mov %%cr0, %0"
        : "=r"(cr0)
    );

    cr0 |= 0x80000000U;

    __asm__ volatile (
        "mov %0, %%cr0"
        :
        : "r"(cr0)
        : "memory"
    );
}

void paging_initialize(void)
{
    for (uint32_t i = 0; i < 1024; i++)
        page_directory[i] = 0;

    for (uint32_t table = 0; table < PAGE_TABLE_COUNT; table++)
    {
        for (uint32_t entry = 0; entry < 1024; entry++)
        {
            uint32_t frame =
                (table * 1024 + entry) * PAGE_SIZE;

            page_tables[table][entry] =
                frame | PAGE_PRESENT | PAGE_WRITE;
        }

        page_directory[table] =
            (uint32_t)&page_tables[table][0]
            | PAGE_PRESENT
            | PAGE_WRITE;
    }

    load_page_directory(page_directory);
    enable_paging();
}
