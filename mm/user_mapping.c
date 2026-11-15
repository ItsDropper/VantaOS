/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

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
    /* Ask for a frame the kernel can address; a higher one would be wasted. */
    void* frame = pmm_alloc_block_below(IDENTITY_MAP_LIMIT);
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
    for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++)
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
    uint64_t* pdpt = NULL;
    uint64_t* pd = NULL;
    uint64_t* pt = NULL;
    int created_pdpt = 0;
    int created_pd = 0;
    int created_pt = 0;

    if (!(pml4[pml4_index] & PAGE_PRESENT))
    {
        if (!create)
            return NULL;

        pdpt = allocate_user_table();
        if (!pdpt)
            return NULL;
        pml4[pml4_index] = (uint64_t)(uintptr_t)pdpt |
                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        created_pdpt = 1;
    }
    else
    {
        if (!(pml4[pml4_index] & PAGE_USER))
            return NULL;
        pdpt = (uint64_t*)(uintptr_t)
            (pml4[pml4_index] & PAGE_ADDRESS_MASK);
    }

    if (!(pdpt[pdpt_index] & PAGE_PRESENT))
    {
        if (!create)
            goto fail;

        pd = allocate_user_table();
        if (!pd)
            goto fail;
        pdpt[pdpt_index] = (uint64_t)(uintptr_t)pd |
                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        created_pd = 1;
    }
    else
    {
        /* A 1 GiB large page is not a page-directory pointer; never walk it. */
        if (!(pdpt[pdpt_index] & PAGE_USER) ||
            (pdpt[pdpt_index] & 0x080U))
            goto fail;
        pd = (uint64_t*)(uintptr_t)
            (pdpt[pdpt_index] & PAGE_ADDRESS_MASK);
    }

    if (!(pd[pd_index] & PAGE_PRESENT))
    {
        if (!create)
            goto fail;

        pt = allocate_user_table();
        if (!pt)
            goto fail;
        pd[pd_index] = (uint64_t)(uintptr_t)pt |
                       PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        created_pt = 1;
    }
    else
    {
        if (!(pd[pd_index] & PAGE_USER) ||
            (pd[pd_index] & 0x080U))
            goto fail;
        pt = (uint64_t*)(uintptr_t)
            (pd[pd_index] & PAGE_ADDRESS_MASK);
    }

    /*
     * A failed walk must not leave newly allocated, unreachable-to-userspace
     * page-table levels consuming PMM frames. Only unwind levels created by
     * this invocation; existing tables and their mappings remain untouched.
     */
    return &pt[pt_index];

fail:
    if (created_pt)
    {
        pd[pd_index] = 0;
        pmm_free_block(pt);
    }
    if (created_pd)
    {
        pdpt[pdpt_index] = 0;
        pmm_free_block(pd);
    }
    if (created_pdpt)
    {
        pml4[pml4_index] = 0;
        pmm_free_block(pdpt);
    }
    return NULL;
}

static volatile uint64_t demand_frames_reclaimed;

uint64_t paging_demand_frames_reclaimed(void)
{
    return __atomic_load_n(&demand_frames_reclaimed, __ATOMIC_ACQUIRE);
}

uint64_t paging_detach_user_page_tables(paging_address_space_t* space)
{
    if (!space || !space->directory)
        return 0U;
    uint64_t entry = space->directory[USER_PML4_INDEX];
    space->directory[USER_PML4_INDEX] = 0U;
    return (entry & PAGE_PRESENT) ? entry : 0U;
}

uint32_t paging_free_detached_user_tables(uint64_t pml4_entry)
{
    if (!(pml4_entry & PAGE_PRESENT))
        return 0U;

    uint32_t demand_freed = 0U;
    uint64_t* pdpt = (uint64_t*)(uintptr_t)(pml4_entry & PAGE_ADDRESS_MASK);

    for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++)
    {
        if (!(pdpt[i] & PAGE_PRESENT) || (pdpt[i] & 0x080U))
            continue;

        uint64_t* pd = (uint64_t*)(uintptr_t)
            (pdpt[i] & PAGE_ADDRESS_MASK);

        for (uint32_t j = 0; j < PAGING_TABLE_ENTRIES; j++)
        {
            if (!(pd[j] & PAGE_PRESENT) || (pd[j] & 0x080U))
                continue;

            uint64_t* pt = (uint64_t*)(uintptr_t)
                (pd[j] & PAGE_ADDRESS_MASK);
            for (uint32_t k = 0; k < PAGING_TABLE_ENTRIES; k++)
            {
                if ((pt[k] & PAGE_PRESENT) && (pt[k] & PAGE_SOFT_DEMAND))
                {
                    pmm_free_block((void*)(uintptr_t)
                                   (pt[k] & PAGE_ADDRESS_MASK));
                    demand_freed++;
                }
            }
            pmm_free_block(pt);
        }

        pmm_free_block(pd);
    }

    pmm_free_block(pdpt);
    if (demand_freed != 0U)
        __atomic_fetch_add(&demand_frames_reclaimed, demand_freed,
                           __ATOMIC_ACQ_REL);
    return demand_freed;
}

paging_demand_result_t paging_map_user_page_demand(
    paging_address_space_t* address_space, uintptr_t virtual_address,
    uintptr_t physical_address, int writable)
{
    if (!paging_nx_available())
        return PAGING_DEMAND_FAILED;

    uint64_t saved_flags = paging_lock_acquire();
    if ((virtual_address & (PAGE_SIZE - 1U)) != 0 ||
        (physical_address & (PAGE_SIZE - 1U)) != 0 ||
        virtual_address < PAGING_USER_BASE ||
        virtual_address > PAGING_USER_END ||
        physical_address >= IDENTITY_MAP_LIMIT ||
        !paging_owns_address_space(address_space))
    {
        paging_lock_release(saved_flags);
        return PAGING_DEMAND_FAILED;
    }

    uint64_t* entry = paging_user_leaf_entry(address_space, virtual_address, 1);
    if (!entry)
    {
        paging_lock_release(saved_flags);
        return PAGING_DEMAND_FAILED;
    }
    if (*entry & PAGE_PRESENT)
    {
        paging_lock_release(saved_flags);
        return PAGING_DEMAND_EXISTS;
    }

    *entry = ((uint64_t)physical_address) | PAGE_PRESENT | PAGE_USER |
             PAGE_NX | PAGE_SOFT_DEMAND | (writable ? PAGE_WRITE : 0U);
    paging_lock_release(saved_flags);
    return PAGING_DEMAND_MAPPED;
}

int paging_map_user_page(paging_address_space_t* address_space,
                         uintptr_t virtual_address,
                         uintptr_t physical_address,
                         int writable)
{
    return paging_map_user_page_ex(address_space, virtual_address,
                                   physical_address, writable, 1);
}

int paging_map_user_page_ex(paging_address_space_t* address_space,
                            uintptr_t virtual_address,
                            uintptr_t physical_address,
                            int writable, int executable)
{
    if (!executable && !paging_nx_available())
        return 0;

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

    uint64_t* entry = paging_user_leaf_entry(address_space, virtual_address, 1);
    if (!entry || (*entry & PAGE_PRESENT))
    {
        paging_lock_release(saved_flags);
        return 0;
    }

    *entry = ((uint64_t)physical_address) |
             PAGE_PRESENT | PAGE_USER |
             (writable ? PAGE_WRITE : 0U) |
             (executable ? 0U : PAGE_NX);
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

    uint64_t* entry = paging_user_leaf_entry(address_space, virtual_address, 0);
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

    /*
     * The caller owns the unmapped physical frame and must free it separately.
     * Never use this on a PAGE_SOFT_DEMAND leaf: those frames are owned by the
     * page tables and would leak; a future munmap must free the frame and
     * adjust the region's resident count itself.
     */
    return 1;
}


int paging_user_range_valid(paging_address_space_t* address_space,
                            uintptr_t virtual_address, size_t length,
                            int writable)
{
    if (!paging_owns_address_space(address_space) || length == 0U ||
        virtual_address < PAGING_USER_BASE ||
        virtual_address > PAGING_USER_END ||
        length - 1U > PAGING_USER_END - virtual_address)
        return 0;

    uintptr_t end = virtual_address + length - 1U;
    uintptr_t page = virtual_address & ~(uintptr_t)(PAGE_SIZE - 1U);
    const uintptr_t last_page = end & ~(uintptr_t)(PAGE_SIZE - 1U);

    for (;;)
    {
        uint64_t* entry = paging_user_leaf_entry(address_space, page, 0);
        if (!entry || !(*entry & PAGE_PRESENT) ||
            !(*entry & PAGE_USER) ||
            (writable && !(*entry & PAGE_WRITE)))
            return 0;

        if (page == last_page)
            return 1;
        if (page > PAGING_USER_END - PAGE_SIZE)
            return 0;
        page += PAGE_SIZE;
    }
}
