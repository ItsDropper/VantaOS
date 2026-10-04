#include "paging.h"
#include "pmm.h"

#include <stddef.h>
#include <stdint.h>

#define ADDRESS_SPACE_MAX 32U
#define PAGE_TABLE_SLOTS 128U
#define IDENTITY_PD_COUNT 4U

static uint64_t kernel_pml4[512] __attribute__((aligned(4096)));
static uint64_t kernel_pdpt[512] __attribute__((aligned(4096)));
static uint64_t kernel_page_directories[IDENTITY_PD_COUNT][512]
    __attribute__((aligned(4096)));

static uint64_t dynamic_page_tables[PAGE_TABLE_SLOTS][512]
    __attribute__((aligned(4096)));
static int dynamic_page_table_used[PAGE_TABLE_SLOTS];
static uint32_t dynamic_page_table_pde[PAGE_TABLE_SLOTS];

static uint64_t address_space_pml4[ADDRESS_SPACE_MAX][512]
    __attribute__((aligned(4096)));

static paging_address_space_t address_spaces[ADDRESS_SPACE_MAX];
static paging_address_space_t kernel_address_space;
static paging_address_space_t* current_address_space;

static inline void load_page_directory(uintptr_t physical_address)
{
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"((uint64_t)physical_address)
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
    uint64_t cr0;

    __asm__ volatile (
        "mov %%cr0, %0"
        : "=r"(cr0)
    );

    cr0 |= 0x80000000ULL;

    __asm__ volatile (
        "mov %0, %%cr0"
        :
        : "r"(cr0)
        : "memory"
    );
}

static uint32_t pml4_index(uintptr_t address)
{
    return (uint32_t)((address >> 39) & 0x1FFU);
}

static uint32_t pdpt_index(uintptr_t address)
{
    return (uint32_t)((address >> 30) & 0x1FFU);
}

static uint32_t pd_index(uintptr_t address)
{
    return (uint32_t)((address >> 21) & 0x1FFU);
}

static uint32_t pt_index(uintptr_t address)
{
    return (uint32_t)((address >> 12) & 0x1FFU);
}

static int find_dynamic_page_table(uint32_t pde_index)
{
    for (uint32_t i = 0; i < PAGE_TABLE_SLOTS; i++)
    {
        if (dynamic_page_table_used[i] &&
            dynamic_page_table_pde[i] == pde_index)
            return (int)i;
    }

    return -1;
}

static int create_dynamic_page_table(uint32_t pdpt_entry, uint32_t pde_index)
{
    int existing = find_dynamic_page_table(pde_index);
    if (existing >= 0)
        return existing;

    if (pdpt_entry >= IDENTITY_PD_COUNT)
        return -1;

    for (uint32_t slot = 0; slot < PAGE_TABLE_SLOTS; slot++)
    {
        if (dynamic_page_table_used[slot])
            continue;

        uint64_t* table = dynamic_page_tables[slot];
        uint64_t* directory =
            kernel_page_directories[pdpt_entry];

        uint64_t large_page_base =
            ((uint64_t)pdpt_entry << 30) |
            ((uint64_t)pde_index << 21);

        for (uint32_t i = 0; i < 512; i++)
        {
            table[i] =
                (large_page_base +
                 ((uint64_t)i << 12)) |
                PAGE_PRESENT |
                PAGE_WRITE;
        }

        directory[pde_index] =
            ((uint64_t)(uintptr_t)table) |
            PAGE_PRESENT |
            PAGE_WRITE;

        dynamic_page_table_used[slot] = 1;
        dynamic_page_table_pde[slot] = pde_index;
        return (int)slot;
    }

    return -1;
}

static uint64_t* get_page_table(uintptr_t virtual_address)
{
    if (pml4_index(virtual_address) != 0)
        return NULL;

    uint32_t pdpt = pdpt_index(virtual_address);
    uint32_t pd = pd_index(virtual_address);

    if (pdpt >= IDENTITY_PD_COUNT)
        return NULL;

    uint64_t pde = kernel_page_directories[pdpt][pd];

    if (!(pde & PAGE_PRESENT))
        return NULL;

    if (pde & PAGE_LARGE)
        return NULL;

    return (uint64_t*)(uintptr_t)
        (pde & 0x000FFFFFFFFFF000ULL);
}

void paging_initialize(void)
{
    for (uint32_t i = 0; i < 512; i++)
        kernel_pml4[i] = 0;

    for (uint32_t i = 0; i < 512; i++)
        kernel_pdpt[i] = 0;

    for (uint32_t table = 0; table < IDENTITY_PD_COUNT; table++)
    {
        for (uint32_t entry = 0; entry < 512; entry++)
        {
            uint64_t frame =
                ((uint64_t)table << 30) |
                ((uint64_t)entry << 21);

            kernel_page_directories[table][entry] =
                frame |
                PAGE_PRESENT |
                PAGE_WRITE |
                PAGE_LARGE;
        }

        kernel_pdpt[table] =
            (uint64_t)(uintptr_t)&kernel_page_directories[table][0] |
            PAGE_PRESENT |
            PAGE_WRITE;
    }

    kernel_pml4[0] =
        (uint64_t)(uintptr_t)kernel_pdpt |
        PAGE_PRESENT |
        PAGE_WRITE;

    for (uint32_t i = 0; i < PAGE_TABLE_SLOTS; i++)
    {
        dynamic_page_table_used[i] = 0;
        dynamic_page_table_pde[i] = 0;
    }

    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX; i++)
    {
        for (uint32_t entry = 0; entry < 512; entry++)
            address_space_pml4[i][entry] = 0;

        address_spaces[i].directory =
            address_space_pml4[i];
        address_spaces[i].directory_physical =
            (uintptr_t)address_space_pml4[i];
        address_spaces[i].used = 0;
    }

    kernel_address_space.directory = kernel_pml4;
    kernel_address_space.directory_physical =
        (uintptr_t)kernel_pml4;
    kernel_address_space.used = 1;

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
        (physical_address % PAGE_SIZE) != 0 ||
        pml4_index(virtual_address) != 0)
        return 0;

    if (current_address_space != &kernel_address_space)
        return 0;

    uint32_t pdpt = pdpt_index(virtual_address);
    uint32_t pd = pd_index(virtual_address);
    uint32_t pt = pt_index(virtual_address);

    if (pdpt >= IDENTITY_PD_COUNT)
        return 0;

    uint64_t* directory =
        kernel_page_directories[pdpt];

    uint64_t pde = directory[pd];

    if (!(pde & PAGE_PRESENT))
        return 0;

    if (pde & PAGE_LARGE)
    {
        if (create_dynamic_page_table(pdpt, pd) < 0)
            return 0;
    }

    uint64_t* table = get_page_table(virtual_address);
    if (!table)
        return 0;

    table[pt] =
        ((uint64_t)physical_address) |
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

    uint64_t* table = get_page_table(virtual_address);
    if (!table)
        return;

    table[pt_index(virtual_address)] = 0;
    invalidate_page(virtual_address);
}

uintptr_t paging_get_physical(uintptr_t virtual_address)
{
    uint32_t pdpt = pdpt_index(virtual_address);
    uint32_t pd = pd_index(virtual_address);

    if (pml4_index(virtual_address) != 0 ||
        pdpt >= IDENTITY_PD_COUNT)
        return 0;

    uint64_t pde =
        kernel_page_directories[pdpt][pd];

    if (!(pde & PAGE_PRESENT))
        return 0;

    if (pde & PAGE_LARGE)
    {
        return (uintptr_t)(
            (pde & 0x000FFFFFFFE00000ULL) |
            (virtual_address & 0x1FFFFFULL)
        );
    }

    uint64_t* table = get_page_table(virtual_address);
    if (!table)
        return 0;

    uint64_t pte = table[pt_index(virtual_address)];

    if (!(pte & PAGE_PRESENT))
        return 0;

    return (uintptr_t)(
        (pte & 0x000FFFFFFFFFF000ULL) |
        (virtual_address & 0xFFFULL)
    );
}

paging_address_space_t* paging_create_address_space(void)
{
    if (current_address_space != &kernel_address_space)
        return NULL;

    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX; i++)
    {
        if (address_spaces[i].used)
            continue;

        /*
         * Share the current kernel PML4 entries. User mappings can be
         * added later without changing the kernel's address space.
         */
        for (uint32_t entry = 0; entry < 512; entry++)
            address_space_pml4[i][entry] = kernel_pml4[entry];

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

    for (uint32_t entry = 0; entry < 512; entry++)
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
