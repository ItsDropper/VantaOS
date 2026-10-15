#include "paging.h"
#include "pmm.h"

#include <stddef.h>
#include <stdint.h>

#define ADDRESS_SPACE_MAX 32U
#define TABLE_ENTRIES 512U
#define PAGE_2M 0x200000ULL

static uint64_t kernel_pml4[TABLE_ENTRIES] __attribute__((aligned(4096)));
static uint64_t kernel_pdpt[TABLE_ENTRIES] __attribute__((aligned(4096)));
static uint64_t kernel_pd[TABLE_ENTRIES] __attribute__((aligned(4096)));
static uint64_t heap_pd[TABLE_ENTRIES] __attribute__((aligned(4096)));
static uint64_t high_pd[TABLE_ENTRIES] __attribute__((aligned(4096)));
static uint64_t heap_pt[TABLE_ENTRIES] __attribute__((aligned(4096)));
static uint64_t graphics_pt[TABLE_ENTRIES] __attribute__((aligned(4096)));

static uint64_t address_space_pml4[ADDRESS_SPACE_MAX][TABLE_ENTRIES]
    __attribute__((aligned(4096)));

static paging_address_space_t address_spaces[ADDRESS_SPACE_MAX];
static paging_address_space_t kernel_address_space;
static paging_address_space_t* current_address_space;

static inline void load_page_table(uintptr_t physical_address)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(physical_address) : "memory");
}

static inline void invalidate_page(uintptr_t address)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(address) : "memory");
}

static uint64_t* table_for(uintptr_t virtual_address)
{
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1FFU;
    uint64_t pd_index = (virtual_address >> 21) & 0x1FFU;

    if (pdpt_index != 3)
        return NULL;

    if (pd_index == ((0xC0000000ULL >> 21) & 0x1FFU))
        return heap_pt;

    if (pd_index == ((0xD0000000ULL >> 21) & 0x1FFU))
        return graphics_pt;

    return NULL;
}


/*
 * User page tables are allocated from physical frames below the current
 * identity-map ceiling. This keeps the kernel able to initialize and walk
 * page-table pages before a general physical direct-map exists.
 */
#define USER_PML4_INDEX 1U
#define IDENTITY_MAP_LIMIT 0x40000000ULL
#define PAGE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL

static int paging_owns_address_space(const paging_address_space_t* space)
{
    if (!space || space == &kernel_address_space || !space->used)
        return 0;

    uintptr_t candidate = (uintptr_t)space;
    uintptr_t first = (uintptr_t)&address_spaces[0];
    uintptr_t end = (uintptr_t)&address_spaces[ADDRESS_SPACE_MAX];

    return candidate >= first && candidate < end &&
           ((candidate - first) % sizeof(address_spaces[0])) == 0;
}

static uint64_t* allocate_user_table(void)
{
    void* frame = pmm_alloc_block();
    if (!frame)
        return NULL;

    uintptr_t physical = (uintptr_t)frame;
    if ((physical & (PAGE_SIZE - 1U)) != 0 ||
        physical >= IDENTITY_MAP_LIMIT)
    {
        pmm_free_block(frame);
        return NULL;
    }

    uint64_t* table = (uint64_t*)physical;
    for (uint32_t i = 0; i < TABLE_ENTRIES; i++)
        table[i] = 0;

    return table;
}

static uint64_t* user_leaf_entry(paging_address_space_t* space,
                                 uintptr_t virtual_address,
                                 int create)
{
    if (!paging_owns_address_space(space) ||
        virtual_address < PAGING_USER_BASE ||
        virtual_address > PAGING_USER_END)
        return NULL;

    uint32_t pml4_index = (uint32_t)((virtual_address >> 39) & 0x1FFU);
    uint32_t pdpt_index = (uint32_t)((virtual_address >> 30) & 0x1FFU);
    uint32_t pd_index = (uint32_t)((virtual_address >> 21) & 0x1FFU);
    uint32_t pt_index = (uint32_t)((virtual_address >> 12) & 0x1FFU);

    if (pml4_index != USER_PML4_INDEX)
        return NULL;

    uint64_t* pml4 = space->directory;
    if (!(pml4[pml4_index] & PAGE_PRESENT))
    {
        if (!create)
            return NULL;

        uint64_t* next = allocate_user_table();
        if (!next)
            return NULL;
        pml4[pml4_index] = (uint64_t)(uintptr_t)next |
                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    if (!(pml4[pml4_index] & PAGE_USER))
        return NULL;
    uint64_t* pdpt = (uint64_t*)(uintptr_t)
        (pml4[pml4_index] & PAGE_ADDRESS_MASK);

    if (!(pdpt[pdpt_index] & PAGE_PRESENT))
    {
        if (!create)
            return NULL;

        uint64_t* next = allocate_user_table();
        if (!next)
            return NULL;
        pdpt[pdpt_index] = (uint64_t)(uintptr_t)next |
                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    if (!(pdpt[pdpt_index] & PAGE_USER))
        return NULL;
    uint64_t* pd = (uint64_t*)(uintptr_t)
        (pdpt[pdpt_index] & PAGE_ADDRESS_MASK);

    if (!(pd[pd_index] & PAGE_PRESENT))
    {
        if (!create)
            return NULL;

        uint64_t* next = allocate_user_table();
        if (!next)
            return NULL;
        pd[pd_index] = (uint64_t)(uintptr_t)next |
                        PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    if (!(pd[pd_index] & PAGE_USER) ||
        (pd[pd_index] & 0x080U))
        return NULL;

    uint64_t* pt = (uint64_t*)(uintptr_t)
        (pd[pd_index] & PAGE_ADDRESS_MASK);
    return &pt[pt_index];
}

static void release_user_page_tables(paging_address_space_t* space)
{
    if (!space || !space->directory)
        return;

    uint64_t* pml4 = space->directory;
    uint64_t pml4_entry = pml4[USER_PML4_INDEX];
    if (!(pml4_entry & PAGE_PRESENT))
        return;

    uint64_t* pdpt = (uint64_t*)(uintptr_t)
        (pml4_entry & PAGE_ADDRESS_MASK);

    for (uint32_t i = 0; i < TABLE_ENTRIES; i++)
    {
        if (!(pdpt[i] & PAGE_PRESENT))
            continue;

        uint64_t* pd = (uint64_t*)(uintptr_t)
            (pdpt[i] & PAGE_ADDRESS_MASK);

        for (uint32_t j = 0; j < TABLE_ENTRIES; j++)
        {
            if (!(pd[j] & PAGE_PRESENT) || (pd[j] & 0x080U))
                continue;

            uint64_t* pt = (uint64_t*)(uintptr_t)
                (pd[j] & PAGE_ADDRESS_MASK);
            pmm_free_block(pt);
        }

        pmm_free_block(pd);
    }

    pmm_free_block(pdpt);
    pml4[USER_PML4_INDEX] = 0;
}

static void map_high_region(uint64_t* pd, uint64_t* pt, uint64_t virtual_base)
{
    uint32_t pd_index = (uint32_t)((virtual_base >> 21) & 0x1FFU);
    pd[pd_index] = ((uint64_t)(uintptr_t)pt) |
                   PAGE_PRESENT | PAGE_WRITE;

    for (uint32_t i = 0; i < TABLE_ENTRIES; i++)
        pt[i] = 0;
}

void paging_initialize(void)
{
    for (uint32_t i = 0; i < TABLE_ENTRIES; i++)
    {
        kernel_pml4[i] = 0;
        kernel_pdpt[i] = 0;
        kernel_pd[i] = 0;
        heap_pd[i] = 0;
        high_pd[i] = 0;
        heap_pt[i] = 0;
        graphics_pt[i] = 0;
    }

    /* Identity-map the first 1 GiB with 2 MiB pages. */
    kernel_pdpt[0] =
        (uint64_t)(uintptr_t)kernel_pd |
        PAGE_PRESENT | PAGE_WRITE;

    for (uint32_t i = 0; i < TABLE_ENTRIES; i++)
        kernel_pd[i] =
            ((uint64_t)i * PAGE_2M) |
            PAGE_PRESENT | PAGE_WRITE | 0x080U;

    /*
     * Keep the existing high virtual windows used by the heap and
     * framebuffer. Both are backed by ordinary 4 KiB page tables.
     */
    kernel_pml4[0] =
        (uint64_t)(uintptr_t)kernel_pdpt |
        PAGE_PRESENT | PAGE_WRITE;

    kernel_pdpt[3] =
        (uint64_t)(uintptr_t)high_pd |
        PAGE_PRESENT | PAGE_WRITE;

    map_high_region(high_pd, heap_pt, 0xC0000000ULL);
    map_high_region(high_pd, graphics_pt, 0xD0000000ULL);

    kernel_address_space.directory = kernel_pml4;
    kernel_address_space.directory_physical =
        (uintptr_t)kernel_pml4;
    kernel_address_space.used = 1;

    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX; i++)
    {
        address_spaces[i].directory = address_space_pml4[i];
        address_spaces[i].directory_physical =
            (uintptr_t)address_space_pml4[i];
        address_spaces[i].used = 0;
    }

    current_address_space = &kernel_address_space;

    load_page_table(kernel_address_space.directory_physical);
}

int paging_map_page(uintptr_t virtual_address, uintptr_t physical_address)
{
    if ((virtual_address % PAGE_SIZE) != 0 ||
        (physical_address % PAGE_SIZE) != 0 ||
        current_address_space != &kernel_address_space)
        return 0;

    uint64_t* table = table_for(virtual_address);
    if (!table)
        return 0;

    uint32_t table_index = (uint32_t)((virtual_address >> 12) & 0x1FFU);
    table[table_index] =
        ((uint64_t)physical_address) |
        PAGE_PRESENT | PAGE_WRITE;

    invalidate_page(virtual_address);
    return 1;
}


int paging_map_user_page(paging_address_space_t* address_space,
                         uintptr_t virtual_address,
                         uintptr_t physical_address,
                         int writable)
{
    if ((virtual_address & (PAGE_SIZE - 1U)) != 0 ||
        (physical_address & (PAGE_SIZE - 1U)) != 0 ||
        virtual_address < PAGING_USER_BASE ||
        virtual_address > PAGING_USER_END ||
        physical_address >= PMM_MAX_PHYSICAL_ADDRESS ||
        !paging_owns_address_space(address_space))
        return 0;

    uint64_t* entry = user_leaf_entry(address_space, virtual_address, 1);
    if (!entry || (*entry & PAGE_PRESENT))
        return 0;

    *entry = ((uint64_t)physical_address) |
             PAGE_PRESENT | PAGE_USER |
             (writable ? PAGE_WRITE : 0U);

    if (current_address_space == address_space)
        invalidate_page(virtual_address);

    return 1;
}

int paging_unmap_user_page(paging_address_space_t* address_space,
                           uintptr_t virtual_address)
{
    if ((virtual_address & (PAGE_SIZE - 1U)) != 0 ||
        !paging_owns_address_space(address_space))
        return 0;

    uint64_t* entry = user_leaf_entry(address_space, virtual_address, 0);
    if (!entry || !(*entry & PAGE_PRESENT))
        return 0;

    *entry = 0;
    if (current_address_space == address_space)
        invalidate_page(virtual_address);

    /* The caller owns the mapped physical frame and must free it separately. */
    return 1;
}

uint64_t paging_get_page_flags(uintptr_t virtual_address)
{
    if (virtual_address >= PAGING_USER_BASE &&
        virtual_address <= PAGING_USER_END)
    {
        uint64_t* entry = user_leaf_entry(current_address_space,
                                          virtual_address, 0);
        return entry ? (*entry & 0xFFFU) : 0;
    }

    uint64_t* table = table_for(virtual_address);
    if (!table)
        return 0;

    uint32_t index = (uint32_t)((virtual_address >> 12) & 0x1FFU);
    return table[index] & 0xFFFU;
}

void paging_unmap_page(uintptr_t virtual_address)
{
    if ((virtual_address % PAGE_SIZE) != 0 ||
        current_address_space != &kernel_address_space)
        return;

    uint64_t* table = table_for(virtual_address);
    if (!table)
        return;

    uint32_t index = (uint32_t)((virtual_address >> 12) & 0x1FFU);
    table[index] = 0;
    invalidate_page(virtual_address);
}

uintptr_t paging_get_physical(uintptr_t virtual_address)
{
    if (virtual_address >= PAGING_USER_BASE &&
        virtual_address <= PAGING_USER_END)
    {
        uint64_t* entry = user_leaf_entry(current_address_space,
                                          virtual_address, 0);
        if (!entry || !(*entry & PAGE_PRESENT))
            return 0;

        return (uintptr_t)(*entry & PAGE_ADDRESS_MASK) |
               (virtual_address & (PAGE_SIZE - 1U));
    }

    uint64_t* table = table_for(virtual_address);

    if (table)
    {
        uint32_t index = (uint32_t)((virtual_address >> 12) & 0x1FFU);
        if (!(table[index] & PAGE_PRESENT))
            return 0;

        return (uintptr_t)(table[index] & 0x000FFFFFFFFFF000ULL) |
               (virtual_address & 0xFFFU);
    }

    /* Identity-mapped low memory. */
    if (virtual_address < 0x40000000ULL)
        return virtual_address;

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

        uint64_t* pml4 = address_spaces[i].directory;

        for (uint32_t entry = 0; entry < TABLE_ENTRIES; entry++)
            pml4[entry] = 0;

        /*
         * Share the kernel's identity and high mappings. User mappings
         * can later occupy otherwise-empty PML4 entries.
         */
        pml4[0] = kernel_pml4[0];

        address_spaces[i].used = 1;
        return &address_spaces[i];
    }

    return NULL;
}

void paging_destroy_address_space(paging_address_space_t* address_space)
{
    if (!paging_owns_address_space(address_space))
        return;

    if (current_address_space == address_space)
        paging_switch_address_space(&kernel_address_space);

    release_user_page_tables(address_space);

    for (uint32_t i = 0; i < TABLE_ENTRIES; i++)
        address_space->directory[i] = 0;

    address_space->used = 0;
}

void paging_switch_address_space(paging_address_space_t* address_space)
{
    if (!address_space || !address_space->used)
        return;

    if (current_address_space == address_space)
        return;

    current_address_space = address_space;
    load_page_table(address_space->directory_physical);
}

paging_address_space_t* paging_get_kernel_address_space(void)
{
    return &kernel_address_space;
}

paging_address_space_t* paging_get_current_address_space(void)
{
    return current_address_space;
}
