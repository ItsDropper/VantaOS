#include "paging.h"
#include "pmm.h"

#include <stdint.h>

#define PAGE_PRESENT  0x001
#define PAGE_WRITE    0x002

#define PAGE_TABLE_COUNT 4
#define HEAP_PAGE_TABLE_INDEX 256

static uint32_t page_directory[1024]
    __attribute__((aligned(4096)));

static uint32_t page_tables[PAGE_TABLE_COUNT][1024]
    __attribute__((aligned(4096)));

static uint32_t heap_page_table[1024]
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

static inline void invalidate_page(uintptr_t address)
{
    __asm__ volatile (
        "invlpg (%0)"
        :
        : "r"(address)
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

static uint32_t* get_page_table(uintptr_t virtual_address)
{
    uint32_t directory_index =
        (uint32_t)(virtual_address >> 22);

    if (directory_index < PAGE_TABLE_COUNT)
        return page_tables[directory_index];

    if (directory_index == HEAP_PAGE_TABLE_INDEX)
        return heap_page_table;

    return NULL;
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

    for (uint32_t entry = 0; entry < 1024; entry++)
        heap_page_table[entry] = 0;

    load_page_directory(page_directory);
    enable_paging();
}

int paging_map_page(
    uintptr_t virtual_address,
    uintptr_t physical_address
)
{
    if ((virtual_address % PAGE_SIZE) != 0 ||
        (physical_address % PAGE_SIZE) != 0)
        return 0;

    uint32_t directory_index =
        (uint32_t)(virtual_address >> 22);

    uint32_t table_index =
        (uint32_t)((virtual_address >> 12) & 0x3FF);

    uint32_t* table = get_page_table(virtual_address);

    if (!table)
        return 0;

    if (directory_index == HEAP_PAGE_TABLE_INDEX &&
        !(page_directory[directory_index] & PAGE_PRESENT))
    {
        page_directory[directory_index] =
            (uint32_t)&heap_page_table[0]
            | PAGE_PRESENT
            | PAGE_WRITE;
    }

    table[table_index] =
        (uint32_t)physical_address | PAGE_PRESENT | PAGE_WRITE;

    invalidate_page(virtual_address);

    return 1;
}

void paging_unmap_page(uintptr_t virtual_address)
{
    if ((virtual_address % PAGE_SIZE) != 0)
        return;

    uint32_t directory_index =
        (uint32_t)(virtual_address >> 22);

    uint32_t table_index =
        (uint32_t)((virtual_address >> 12) & 0x3FF);

    uint32_t* table = get_page_table(virtual_address);

    if (!table ||
        !(page_directory[directory_index] & PAGE_PRESENT))
        return;

    table[table_index] = 0;
    invalidate_page(virtual_address);
}

uintptr_t paging_get_physical(uintptr_t virtual_address)
{
    uint32_t directory_index =
        (uint32_t)(virtual_address >> 22);

    uint32_t table_index =
        (uint32_t)((virtual_address >> 12) & 0x3FF);

    uint32_t offset =
        (uint32_t)(virtual_address & 0xFFF);

    uint32_t* table = get_page_table(virtual_address);

    if (!table ||
        !(page_directory[directory_index] & PAGE_PRESENT) ||
        !(table[table_index] & PAGE_PRESENT))
        return 0;

    return (uintptr_t)(table[table_index] & 0xFFFFF000U) + offset;
}
