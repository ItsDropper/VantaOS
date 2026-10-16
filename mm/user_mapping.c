#include "paging_internal.h"
#include "pmm.h"

#include <stddef.h>
#include <stdint.h>

#define USER_PML4_INDEX 1U
#define IDENTITY_MAP_LIMIT 0x40000000ULL

/*
 * User page tables are allocated from physical frames below the current
 * identity-map ceiling. This keeps the kernel able to initialize and walk
 * page-table pages before a general physical direct-map exists.
 */

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
    for (uint32_t i = 0; i < PAGING_PAGING_TABLE_ENTRIES; i++)
        table[i] = 0;

    return table;
}

uint64_t* paging_user_leaf_entry(paging_address_space_t* space,
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

void paging_release_user_page_tables(paging_address_space_t* space)
{
    if (!space || !space->directory)
        return;

    uint64_t* pml4 = space->directory;
    uint64_t pml4_entry = pml4[USER_PML4_INDEX];
    if (!(pml4_entry & PAGE_PRESENT))
        return;

    uint64_t* pdpt = (uint64_t*)(uintptr_t)
        (pml4_entry & PAGE_ADDRESS_MASK);

    for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++)
    {
        if (!(pdpt[i] & PAGE_PRESENT))
            continue;

        uint64_t* pd = (uint64_t*)(uintptr_t)
            (pdpt[i] & PAGE_ADDRESS_MASK);

        for (uint32_t j = 0; j < PAGING_TABLE_ENTRIES; j++)
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


int paging_map_user_page(paging_address_space_t* address_space,
                         uintptr_t virtual_address,
                         uintptr_t physical_address,
                         int writable)
{
    uint64_t saved_flags = paging_lock_acquire();
    if ((virtual_address & (PAGE_SIZE - 1U)) != 0 ||
        (physical_address & (PAGE_SIZE - 1U)) != 0 ||
        virtual_address < PAGING_USER_BASE ||
        virtual_address > PAGING_USER_END ||
        physical_address >= PMM_MAX_PHYSICAL_ADDRESS ||
        !paging_owns_address_space(address_space))
    {
        paging_lock_release(saved_flags);
        return 0;
    }

    uint64_t* entry = user_leaf_entry(address_space, virtual_address, 1);
    if (!entry || (*entry & PAGE_PRESENT))
    {
        paging_lock_release(saved_flags);
        return 0;
    }

    *entry = ((uint64_t)physical_address) |
             PAGE_PRESENT | PAGE_USER |
             (writable ? PAGE_WRITE : 0U);
    __atomic_add_fetch(&address_space->pending_shootdowns, 1U,
                       __ATOMIC_RELEASE);
    paging_lock_release(saved_flags);

    if (!paging_tlb_shootdown(address_space, virtual_address))
        paging_tlb_failure();
    __atomic_sub_fetch(&address_space->pending_shootdowns, 1U,
                       __ATOMIC_RELEASE);
    return 1;
}

int paging_unmap_user_page(paging_address_space_t* address_space,
                           uintptr_t virtual_address)
{
    uint64_t saved_flags = paging_lock_acquire();
    if ((virtual_address & (PAGE_SIZE - 1U)) != 0 ||
        !paging_owns_address_space(address_space))
    {
        paging_lock_release(saved_flags);
        return 0;
    }

    uint64_t* entry = user_leaf_entry(address_space, virtual_address, 0);
    if (!entry || !(*entry & PAGE_PRESENT))
    {
        paging_lock_release(saved_flags);
        return 0;
    }

    *entry = 0;
    __atomic_add_fetch(&address_space->pending_shootdowns, 1U,
                       __ATOMIC_RELEASE);
    paging_lock_release(saved_flags);

    if (!paging_tlb_shootdown(address_space, virtual_address))
        paging_tlb_failure();
    __atomic_sub_fetch(&address_space->pending_shootdowns, 1U,
                       __ATOMIC_RELEASE);

    /* The caller owns the unmapped physical frame and must free it separately. */
    return 1;
}


