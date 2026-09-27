#include "paging.h"
#include "pmm.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_TABLE_COUNT 4U
#define ADDRESS_SPACE_MAX 32U

static uint32_t kernel_page_directory[1024]
    __attribute__((aligned(4096)));

static uint32_t kernel_page_tables[PAGE_TABLE_COUNT][1024]
    __attribute__((aligned(4096)));

static uint32_t heap_page_table[1024]
    __attribute__((aligned(4096)));

static uint32_t graphics_page_table[1024]
    __attribute__((aligned(4096)));

/*
 * Address-space directories are deliberately kept in kernel memory for
 * this stage. They are page-aligned and therefore directly usable as
 * CR3 values while the kernel still uses its identity mapping.
 *
 * User page tables are intentionally not allocated yet. The lower
 * address-space entries remain empty until the user-memory layer is
 * introduced. This gives processes independent page directories without
 * enabling an unverified user-mode path.
 */
static uint32_t address_space_directories[ADDRESS_SPACE_MAX][1024]
    __attribute__((aligned(4096)));

static paging_address_space_t address_spaces[ADDRESS_SPACE_MAX];

static paging_address_space_t kernel_address_space;
static paging_address_space_t* current_address_space;

static inline void load_page_directory(uintptr_t physical_address)
{
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"((uint32_t)physical_address)
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

static uint32_t* get_kernel_page_table(uintptr_t virtual_address)
{
    uint32_t directory_index =
        (uint32_t)(virtual_address >> 22);

    if (directory_index < PAGE_TABLE_COUNT)
        return kernel_page_tables[directory_index];

    if (directory_index == PAGING_KERNEL_DIRECTORY_INDEX)
        return heap_page_table;

    if (directory_index == PAGING_GRAPHICS_DIRECTORY_INDEX)
        return graphics_page_table;

    return NULL;
}

static int is_kernel_mapping(uint32_t directory_index)
{
    return directory_index < PAGE_TABLE_COUNT ||
           directory_index == PAGING_KERNEL_DIRECTORY_INDEX ||
           directory_index == PAGING_GRAPHICS_DIRECTORY_INDEX;
}

void paging_initialize(void)
{
    for (uint32_t i = 0; i < 1024; i++)
        kernel_page_directory[i] = 0;

    for (uint32_t table = 0; table < PAGE_TABLE_COUNT; table++)
    {
        for (uint32_t entry = 0; entry < 1024; entry++)
        {
            uint32_t frame =
                (table * 1024U + entry) * PAGE_SIZE;

            kernel_page_tables[table][entry] =
                frame | PAGE_PRESENT | PAGE_WRITE;
        }

        kernel_page_directory[table] =
            (uint32_t)&kernel_page_tables[table][0] |
            PAGE_PRESENT |
            PAGE_WRITE;
    }

    for (uint32_t entry = 0; entry < 1024; entry++)
    {
        heap_page_table[entry] = 0;
        graphics_page_table[entry] = 0;
    }

    kernel_page_directory[PAGING_KERNEL_DIRECTORY_INDEX] = 0;
    kernel_page_directory[PAGING_GRAPHICS_DIRECTORY_INDEX] = 0;

    kernel_address_space.directory =
        kernel_page_directory;
    kernel_address_space.directory_physical =
        (uintptr_t)kernel_page_directory;
    kernel_address_space.used = 1;

    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX; i++)
    {
        address_spaces[i].directory = address_space_directories[i];
        address_spaces[i].directory_physical =
            (uintptr_t)address_space_directories[i];
        address_spaces[i].used = 0;
    }

    current_address_space = &kernel_address_space;

    load_page_directory(
        kernel_address_space.directory_physical
    );

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
        (uint32_t)((virtual_address >> 12) & 0x3FFU);

    uint32_t* table =
        get_kernel_page_table(virtual_address);

    if (!table ||
        current_address_space != &kernel_address_space)
        return 0;

    if (directory_index == PAGING_KERNEL_DIRECTORY_INDEX &&
        !(kernel_page_directory[directory_index] & PAGE_PRESENT))
    {
        kernel_page_directory[directory_index] =
            (uint32_t)&heap_page_table[0] |
            PAGE_PRESENT |
            PAGE_WRITE;
    }

    if (directory_index == PAGING_GRAPHICS_DIRECTORY_INDEX &&
        !(kernel_page_directory[directory_index] & PAGE_PRESENT))
    {
        kernel_page_directory[directory_index] =
            (uint32_t)&graphics_page_table[0] |
            PAGE_PRESENT |
            PAGE_WRITE;
    }

    table[table_index] =
        (uint32_t)physical_address |
        PAGE_PRESENT |
        PAGE_WRITE;

    invalidate_page(virtual_address);

    return 1;
}

void paging_unmap_page(uintptr_t virtual_address)
{
    if ((virtual_address % PAGE_SIZE) != 0 ||
        current_address_space != &kernel_address_space)
        return;

    uint32_t directory_index =
        (uint32_t)(virtual_address >> 22);

    uint32_t table_index =
        (uint32_t)((virtual_address >> 12) & 0x3FFU);

    uint32_t* table =
        get_kernel_page_table(virtual_address);

    if (!table ||
        !(kernel_page_directory[directory_index] & PAGE_PRESENT))
        return;

    table[table_index] = 0;
    invalidate_page(virtual_address);
}

uintptr_t paging_get_physical(uintptr_t virtual_address)
{
    uint32_t directory_index =
        (uint32_t)(virtual_address >> 22);

    uint32_t table_index =
        (uint32_t)((virtual_address >> 12) & 0x3FFU);

    uint32_t offset =
        (uint32_t)(virtual_address & 0xFFFU);

    if (current_address_space == &kernel_address_space)
    {
        uint32_t* table =
            get_kernel_page_table(virtual_address);

        if (!table ||
            !(kernel_page_directory[directory_index] & PAGE_PRESENT) ||
            !(table[table_index] & PAGE_PRESENT))
            return 0;

        return (uintptr_t)
            (table[table_index] & 0xFFFFF000U) + offset;
    }

    /*
     * User address spaces intentionally have no user mappings yet.
     * Kernel mappings can still be inspected after a future CR3 switch
     * once the shared kernel mapping layer is expanded.
     */
    return 0;
}

paging_address_space_t* paging_create_address_space(void)
{
    if (current_address_space != &kernel_address_space)
        return NULL;

    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX; i++)
    {
        if (address_spaces[i].used)
            continue;

        uint32_t* directory =
            address_spaces[i].directory;

        for (uint32_t entry = 0; entry < 1024; entry++)
            directory[entry] = 0;

        /*
         * Keep the existing supervisor-only kernel mappings shared.
         * The user portion (PDE 0..767) starts empty.
         */
        for (uint32_t entry = 768; entry < 1024; entry++)
        {
            if (kernel_page_directory[entry] & PAGE_PRESENT)
                directory[entry] =
                    kernel_page_directory[entry];
        }

        /*
         * VantaOS is still in the transition from its identity-mapped
         * bootstrap kernel. Keep the current low identity mappings
         * supervisor-only so the kernel can continue operating while
         * the user-memory layer is developed.
         */
        for (uint32_t entry = 0; entry < PAGE_TABLE_COUNT; entry++)
            directory[entry] = kernel_page_directory[entry];

        directory[PAGING_KERNEL_DIRECTORY_INDEX] =
            kernel_page_directory[PAGING_KERNEL_DIRECTORY_INDEX];

        directory[PAGING_GRAPHICS_DIRECTORY_INDEX] =
            kernel_page_directory[PAGING_GRAPHICS_DIRECTORY_INDEX];

        address_spaces[i].used = 1;
        return &address_spaces[i];
    }

    return NULL;
}

void paging_destroy_address_space(
    paging_address_space_t* address_space
)
{
    if (!address_space ||
        address_space == &kernel_address_space)
        return;

    address_space->used = 0;

    for (uint32_t entry = 0; entry < 1024; entry++)
        address_space->directory[entry] = 0;
}

void paging_switch_address_space(
    paging_address_space_t* address_space
)
{
    if (!address_space ||
        !address_space->used)
        return;

    if (current_address_space == address_space)
        return;

    current_address_space = address_space;

    load_page_directory(
        address_space->directory_physical
    );
}

paging_address_space_t* paging_get_kernel_address_space(void)
{
    return &kernel_address_space;
}

paging_address_space_t* paging_get_current_address_space(void)
{
    return current_address_space;
}
