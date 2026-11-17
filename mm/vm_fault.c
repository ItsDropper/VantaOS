/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "vm_fault.h"
#include "paging_internal.h"
#include "pmm.h"

#include <stddef.h>
#include <stdint.h>

#define VM_IDENTITY_LIMIT 0x40000000ULL

/*
 * Demand paging for user address spaces.
 *
 * Locking: region lookup takes only the per-address-space vm lock, which is
 * released before any allocation. Zeroing happens with no lock held. Only the
 * brief PTE install takes the existing paging mutation lock, shared with
 * every other page-table update; no new global lock is introduced. A process
 * executes on one CPU at a time, so two CPUs never fault in the same address
 * space concurrently; the EXISTS path still covers a prefault/fault overlap.
 */

static volatile uint64_t stat_resolved;
static volatile uint64_t stat_already;
static volatile uint64_t stat_fatal;
static volatile uint64_t stat_oom;
static volatile uint64_t stat_prefaulted;
static volatile uint64_t stat_class[VM_FAULT_NOT_DEMAND + 1];
static volatile uint32_t inject_failures;
static volatile uintptr_t inject_window_start;
static volatile uintptr_t inject_window_end;

void vm_fault_inject_alloc_failures(uintptr_t window_start, size_t window_length,
                                    uint32_t count)
{
    __atomic_store_n(&inject_failures, 0U, __ATOMIC_RELEASE);
    __atomic_store_n(&inject_window_start, window_start, __ATOMIC_RELEASE);
    __atomic_store_n(&inject_window_end, window_start + window_length,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&inject_failures, count, __ATOMIC_RELEASE);
}

uint32_t vm_fault_injected_failures_pending(void)
{
    return __atomic_load_n(&inject_failures, __ATOMIC_ACQUIRE);
}

void vm_fault_get_stats(vm_fault_stats_t* out)
{
    if (!out)
        return;
    out->resolved = __atomic_load_n(&stat_resolved, __ATOMIC_RELAXED);
    out->already_mapped = __atomic_load_n(&stat_already, __ATOMIC_RELAXED);
    out->fatal = __atomic_load_n(&stat_fatal, __ATOMIC_RELAXED);
    out->out_of_memory = __atomic_load_n(&stat_oom, __ATOMIC_RELAXED);
    out->prefaulted = __atomic_load_n(&stat_prefaulted, __ATOMIC_RELAXED);
    for (uint32_t i = 0U; i <= VM_FAULT_NOT_DEMAND; i++)
        out->by_class[i] = __atomic_load_n(&stat_class[i], __ATOMIC_RELAXED);
}

static int take_injected_failure(uintptr_t page)
{
    /* Scoped to a window so unrelated processes never consume an armed failure. */
    if (page < __atomic_load_n(&inject_window_start, __ATOMIC_ACQUIRE) ||
        page >= __atomic_load_n(&inject_window_end, __ATOMIC_ACQUIRE))
        return 0;
    uint32_t pending = __atomic_load_n(&inject_failures, __ATOMIC_ACQUIRE);
    while (pending != 0U)
    {
        if (__atomic_compare_exchange_n(&inject_failures, &pending,
                                        pending - 1U, 0, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE))
            return 1;
    }
    return 0;
}

/* Allocate, zero, and install one demand page; no locks held on entry. */
static vm_fault_result_t populate_page(paging_address_space_t* space,
                                       uintptr_t page,
                                       const vm_region_t* region)
{
    if (take_injected_failure(page))
        return VM_FAULT_OUT_OF_MEMORY;

    void* frame = pmm_alloc_block_below(VM_IDENTITY_LIMIT);
    if (!frame)
        return VM_FAULT_OUT_OF_MEMORY;
    if (((uintptr_t)frame & (PAGE_SIZE - 1U)) != 0U)
    {
        pmm_free_block(frame);
        return VM_FAULT_OUT_OF_MEMORY;
    }

    /* Zero before publication: bytes outside the file-backed extent (including
       a partial-page .bss tail) must never expose stale physical memory. */
    volatile uint64_t* words = (volatile uint64_t*)frame;
    for (size_t i = 0U; i < PAGE_SIZE / sizeof(uint64_t); i++)
        words[i] = 0U;
    if (region->backing == VM_BACKING_FILE)
    {
        uintptr_t page_offset = page - region->start;
        if (!region->file_data || page_offset >= region->file_bytes)
        {
            pmm_free_block(frame);
            return VM_FAULT_FATAL;
        }
        uint64_t remaining = region->file_bytes - page_offset;
        size_t copy_bytes = remaining < PAGE_SIZE ?
                            (size_t)remaining : PAGE_SIZE;
        const uint8_t* source = region->file_data +
                                region->file_offset + page_offset;
        uint8_t* destination = (uint8_t*)frame;
        for (size_t i = 0U; i < copy_bytes; i++)
            destination[i] = source[i];
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);

    switch (paging_map_user_page_demand(
        space, page, (uintptr_t)frame,
        (region->prot & VM_PROT_WRITE) != 0U))
    {
    case PAGING_DEMAND_MAPPED:
        vm_space_note_resident(&space->vm, page, 1);
        return VM_FAULT_RESOLVED;
    case PAGING_DEMAND_EXISTS:
        pmm_free_block(frame);
        __atomic_fetch_add(&stat_already, 1U, __ATOMIC_RELAXED);
        return VM_FAULT_RESOLVED;
    default:
        pmm_free_block(frame);
        return VM_FAULT_OUT_OF_MEMORY;
    }
}

vm_fault_result_t vm_handle_user_fault(paging_address_space_t* space,
                                       uintptr_t cr2, uint64_t error_code,
                                       vm_fault_class_t* class_out)
{
    vm_region_t region = {0};
    vm_fault_class_t cls = VM_FAULT_NO_REGION;
    vm_fault_result_t result = VM_FAULT_FATAL;

    if (space && paging_owns_address_space(space))
        cls = vm_space_classify_fault(&space->vm, cr2, error_code, &region);

    if (cls == VM_FAULT_DEMAND_MAP)
        result = populate_page(space, cr2 & ~(uintptr_t)(PAGE_SIZE - 1U),
                               &region);

    if (space && paging_owns_address_space(space))
    {
        __atomic_fetch_add(&space->vm.fault_events, 1U, __ATOMIC_RELAXED);
        __atomic_store_n(&space->vm.last_fault_class, (uint32_t)cls,
                         __ATOMIC_RELAXED);
        __atomic_store_n(&space->vm.last_fault_address, (uint64_t)cr2,
                         __ATOMIC_RELAXED);
        __atomic_store_n(&space->vm.last_fault_error, error_code,
                         __ATOMIC_RELAXED);
    }

    __atomic_fetch_add(&stat_class[cls], 1U, __ATOMIC_RELAXED);
    if (result == VM_FAULT_RESOLVED)
        __atomic_fetch_add(&stat_resolved, 1U, __ATOMIC_RELAXED);
    else if (result == VM_FAULT_OUT_OF_MEMORY)
        __atomic_fetch_add(&stat_oom, 1U, __ATOMIC_RELAXED);
    else
        __atomic_fetch_add(&stat_fatal, 1U, __ATOMIC_RELAXED);

    if (class_out)
        *class_out = cls;
    return result;
}

int vm_user_range_ensure(paging_address_space_t* space, uintptr_t address,
                         size_t length, int writable)
{
    if (!space || !paging_owns_address_space(space) || length == 0U ||
        address < PAGING_USER_BASE || address > PAGING_USER_END ||
        length - 1U > PAGING_USER_END - address)
        return 0;

    const uintptr_t last =
        (address + length - 1U) & ~(uintptr_t)(PAGE_SIZE - 1U);
    uintptr_t page = address & ~(uintptr_t)(PAGE_SIZE - 1U);

    for (;;)
    {
        uint64_t* entry = paging_user_leaf_entry(space, page, 0);
        int present = entry && (*entry & PAGE_PRESENT) != 0U;
        if (!present)
        {
            vm_region_t region;
            if (!vm_space_find_region(&space->vm, page, &region) ||
                (region.backing != VM_BACKING_ZERO &&
                 region.backing != VM_BACKING_FILE) ||
                (region.prot & (writable ? VM_PROT_WRITE : VM_PROT_READ)) == 0U ||
                populate_page(space, page, &region) != VM_FAULT_RESOLVED)
                return 0;
            __atomic_fetch_add(&stat_prefaulted, 1U, __ATOMIC_RELAXED);
        }
        if (page == last)
            return 1;
        page += PAGE_SIZE;
    }
}

int vm_user_range_valid(paging_address_space_t* space, uintptr_t address,
                        size_t length, int writable)
{
    return vm_user_range_ensure(space, address, length, writable) &&
           paging_user_range_valid(space, address, length, writable);
}
