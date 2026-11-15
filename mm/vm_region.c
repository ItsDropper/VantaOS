/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "vm_region.h"
#include "paging.h"

/*
 * Region bookkeeping only. No hardware access happens here so the logic can
 * be exercised exhaustively by host tests (scripts/test_vm_region.c).
 */

#define VM_PAGE_MASK ((uintptr_t)PAGE_SIZE - 1U)
#define VM_USER_LIMIT ((uint64_t)PAGING_USER_END + 1ULL) /* exclusive */

void vm_space_init(vm_space_t* space)
{
    if (!space)
        return;
    space->lock = (spinlock_t)SPINLOCK_INITIALIZER;
    space->region_count = 0U;
    space->reserved_bytes = 0U;
    space->demand_faults = 0U;
    space->resident_pages = 0U;
    space->fault_events = 0U;
    space->last_fault_class = VM_FAULT_NO_REGION;
    space->last_fault_address = 0U;
    space->last_fault_error = 0U;
    for (uint32_t i = 0U; i < VM_REGION_MAX; i++)
        space->regions[i] = (vm_region_t){0};
}

void vm_space_clear(vm_space_t* space)
{
    if (!space)
        return;
    uint64_t flags = spinlock_lock_irqsave(&space->lock);
    space->region_count = 0U;
    space->reserved_bytes = 0U;
    space->resident_pages = 0U;
    for (uint32_t i = 0U; i < VM_REGION_MAX; i++)
        space->regions[i] = (vm_region_t){0};
    spinlock_unlock_irqrestore(&space->lock, flags);
}

int vm_region_validate(uintptr_t start, size_t length, uint32_t prot,
                       vm_region_type_t type, vm_backing_t backing)
{
    if (length == 0U || (start & VM_PAGE_MASK) != 0U ||
        (length & VM_PAGE_MASK) != 0U)
        return 0;
    /* Whole range must sit inside the user window; no wrap, no kernel hole. */
    if (start < PAGING_USER_BASE || (uint64_t)start >= VM_USER_LIMIT ||
        (uint64_t)length > VM_USER_LIMIT - (uint64_t)start)
        return 0;
    if ((uint64_t)length > VM_SPACE_MAX_RESERVED_BYTES)
        return 0;
    if ((prot & ~VM_PROT_MASK) != 0U)
        return 0;
    /* x86 cannot express write-only pages; writable memory is readable. */
    if ((prot & VM_PROT_WRITE) != 0U && (prot & VM_PROT_READ) == 0U)
        return 0;
    /* Writable and executable at once is never allowed (W^X). */
    if ((prot & (VM_PROT_WRITE | VM_PROT_EXEC)) ==
        (VM_PROT_WRITE | VM_PROT_EXEC))
        return 0;

    switch (type)
    {
    case VM_REGION_GUARD:
        return prot == 0U && backing == VM_BACKING_NONE;
    case VM_REGION_ANON:
    case VM_REGION_STACK:
        /* Demand-zero memory is never executable. */
        return backing == VM_BACKING_ZERO && prot != 0U &&
               (prot & VM_PROT_EXEC) == 0U;
    case VM_REGION_ELF:
        if (backing == VM_BACKING_ZERO)
            return (prot & VM_PROT_EXEC) == 0U && prot != 0U;
        return backing == VM_BACKING_POPULATED && prot != 0U;
    case VM_REGION_FLAT_IMAGE:
        return backing == VM_BACKING_POPULATED && prot != 0U;
    default:
        return 0;
    }
}

int vm_space_add_region(vm_space_t* space, uintptr_t start, size_t length,
                        uint32_t prot, vm_region_type_t type,
                        vm_backing_t backing)
{
    if (!space || !vm_region_validate(start, length, prot, type, backing))
        return 0;

    const uintptr_t end = start + length; /* validated: cannot wrap */
    int added = 0;
    uint64_t flags = spinlock_lock_irqsave(&space->lock);

    /* Guards reserve address space but never memory, so they cost no budget. */
    const uint64_t charge = type == VM_REGION_GUARD ? 0U : (uint64_t)length;
    if (space->region_count < VM_REGION_MAX &&
        space->reserved_bytes + charge <= VM_SPACE_MAX_RESERVED_BYTES)
    {
        int overlap = 0;
        for (uint32_t i = 0U; i < space->region_count; i++)
        {
            const vm_region_t* r = &space->regions[i];
            if (start < r->end && r->start < end)
            {
                overlap = 1;
                break;
            }
        }
        if (!overlap)
        {
            vm_region_t* r = &space->regions[space->region_count++];
            r->start = start;
            r->end = end;
            r->type = (uint8_t)type;
            r->prot = (uint8_t)prot;
            r->backing = (uint8_t)backing;
            r->reserved = 0U;
            r->resident_pages = 0U;
            space->reserved_bytes += charge;
            added = 1;
        }
    }

    spinlock_unlock_irqrestore(&space->lock, flags);
    return added;
}

static const vm_region_t* find_locked(const vm_space_t* space,
                                      uintptr_t address)
{
    for (uint32_t i = 0U; i < space->region_count; i++)
    {
        const vm_region_t* r = &space->regions[i];
        if (address >= r->start && address < r->end)
            return r;
    }
    return NULL;
}

int vm_space_find_region(vm_space_t* space, uintptr_t address,
                         vm_region_t* out)
{
    if (!space)
        return 0;
    int found = 0;
    uint64_t flags = spinlock_lock_irqsave(&space->lock);
    const vm_region_t* r = find_locked(space, address);
    if (r)
    {
        if (out)
            *out = *r;
        found = 1;
    }
    spinlock_unlock_irqrestore(&space->lock, flags);
    return found;
}

vm_fault_class_t vm_space_classify_fault(vm_space_t* space, uintptr_t address,
                                         uint64_t error_code,
                                         vm_region_t* region_out)
{
    if ((error_code & VM_PF_RESERVED) != 0U)
        return VM_FAULT_RESERVED_BIT;
    if ((error_code & VM_PF_USER) == 0U)
        return VM_FAULT_NOT_USER_MODE;

    vm_region_t region;
    if (address < PAGING_USER_BASE || (uint64_t)address >= VM_USER_LIMIT ||
        !vm_space_find_region(space, address, &region))
        return VM_FAULT_NO_REGION;

    if (region.type == VM_REGION_GUARD)
        return VM_FAULT_GUARD;

    /* A present page that still faulted is a pure permission violation. */
    if ((error_code & VM_PF_PRESENT) != 0U)
        return VM_FAULT_PROTECTION;

    if ((error_code & VM_PF_INSTR) != 0U)
    {
        if ((region.prot & VM_PROT_EXEC) == 0U)
            return VM_FAULT_ACCESS_DENIED;
    }
    else if ((error_code & VM_PF_WRITE) != 0U)
    {
        if ((region.prot & VM_PROT_WRITE) == 0U)
            return VM_FAULT_ACCESS_DENIED;
    }
    else if ((region.prot & VM_PROT_READ) == 0U)
        return VM_FAULT_ACCESS_DENIED;

    /*
     * Eagerly populated pages are never legitimately absent; a miss there
     * means the mapping was lost or never made, which must not be papered
     * over by inventing a zero page where file data belongs.
     */
    if (region.backing != VM_BACKING_ZERO)
        return VM_FAULT_NOT_DEMAND;

    if (region_out)
        *region_out = region;
    return VM_FAULT_DEMAND_MAP;
}

void vm_space_note_resident(vm_space_t* space, uintptr_t address,
                            int32_t delta)
{
    if (!space)
        return;
    uint64_t flags = spinlock_lock_irqsave(&space->lock);
    for (uint32_t i = 0U; i < space->region_count; i++)
    {
        vm_region_t* r = &space->regions[i];
        if (address >= r->start && address < r->end)
        {
            if (delta >= 0)
            {
                r->resident_pages += (uint32_t)delta;
                space->resident_pages += (uint32_t)delta;
                space->demand_faults += (uint32_t)delta;
            }
            else if (r->resident_pages >= (uint32_t)(-delta))
            {
                r->resident_pages -= (uint32_t)(-delta);
                space->resident_pages -= (uint32_t)(-delta);
            }
            break;
        }
    }
    spinlock_unlock_irqrestore(&space->lock, flags);
}

const char* vm_fault_class_name(vm_fault_class_t cls)
{
    switch (cls)
    {
    case VM_FAULT_DEMAND_MAP:     return "demand-map";
    case VM_FAULT_ALREADY_MAPPED: return "already-mapped";
    case VM_FAULT_NO_REGION:      return "no-region";
    case VM_FAULT_GUARD:          return "guard";
    case VM_FAULT_PROTECTION:     return "protection";
    case VM_FAULT_ACCESS_DENIED:  return "access-denied";
    case VM_FAULT_RESERVED_BIT:   return "reserved-bit";
    case VM_FAULT_NOT_USER_MODE:  return "not-user-mode";
    case VM_FAULT_NOT_DEMAND:     return "not-demand";
    }
    return "unknown";
}
