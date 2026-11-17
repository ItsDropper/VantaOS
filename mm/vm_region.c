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
    space->brk_base = 0U;
    space->brk_cur = 0U;
    space->image_backing = NULL;
    space->image_backing_size = 0U;
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
    space->brk_base = 0U;
    space->brk_cur = 0U;
    space->image_backing = NULL;
    space->image_backing_size = 0U;
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
        if (backing == VM_BACKING_ZERO || backing == VM_BACKING_FILE)
            return (prot & VM_PROT_EXEC) == 0U && prot != 0U;
        return backing == VM_BACKING_POPULATED && prot != 0U;
    case VM_REGION_FLAT_IMAGE:
        return backing == VM_BACKING_POPULATED && prot != 0U;
    default:
        return 0;
    }
}

static int overlaps_locked(const vm_space_t* space, uintptr_t start,
                           uintptr_t end)
{
    for (uint32_t i = 0U; i < space->region_count; i++)
    {
        const vm_region_t* r = &space->regions[i];
        if (start < r->end && r->start < end)
            return 1;
    }
    return 0;
}

/* Validate, check slot/budget/overlap, insert. Caller holds space->lock. */
static int add_locked(vm_space_t* space, uintptr_t start, size_t length,
                      uint32_t prot, vm_region_type_t type,
                      vm_backing_t backing, uint8_t flags)
{
    if (!vm_region_validate(start, length, prot, type, backing))
        return 0;

    const uintptr_t end = start + length; /* validated: cannot wrap */
    /* Guards reserve address space but never memory, so they cost no budget. */
    const uint64_t charge = type == VM_REGION_GUARD ? 0U : (uint64_t)length;
    if (space->region_count >= VM_REGION_MAX ||
        space->reserved_bytes + charge > VM_SPACE_MAX_RESERVED_BYTES ||
        overlaps_locked(space, start, end))
        return 0;

    vm_region_t* r = &space->regions[space->region_count++];
    r->start = start;
    r->end = end;
    r->type = (uint8_t)type;
    r->prot = (uint8_t)prot;
    r->backing = (uint8_t)backing;
    r->flags = flags;
    r->resident_pages = 0U;
    r->file_data = NULL;
    r->file_offset = 0U;
    r->file_bytes = 0U;
    r->file_prefix = 0U;
    space->reserved_bytes += charge;
    return 1;
}

int vm_space_add_region(vm_space_t* space, uintptr_t start, size_t length,
                        uint32_t prot, vm_region_type_t type,
                        vm_backing_t backing)
{
    /* FILE regions require the image-range checks in vm_space_add_file_region. */
    if (!space || backing == VM_BACKING_FILE)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&space->lock);
    int added = add_locked(space, start, length, prot, type, backing, 0U);
    spinlock_unlock_irqrestore(&space->lock, flags);
    return added;
}

int vm_space_set_image_backing(vm_space_t* space, const void* image,
                               size_t image_size)
{
    if (!space || !image || image_size == 0U)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&space->lock);
    int ok = space->image_backing == NULL && space->image_backing_size == 0U;
    if (ok)
    {
        space->image_backing = (const uint8_t*)image;
        space->image_backing_size = image_size;
    }
    spinlock_unlock_irqrestore(&space->lock, flags);
    return ok;
}

int vm_space_add_file_region(vm_space_t* space, uintptr_t start,
                             size_t length, uint32_t prot,
                             uint64_t file_offset, uint32_t file_prefix,
                             uint64_t file_bytes)
{
    if (!space || file_bytes == 0U)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&space->lock);
    int valid = space->image_backing != NULL &&
                file_offset <= space->image_backing_size &&
                file_bytes <= space->image_backing_size - file_offset &&
                file_prefix < PAGE_SIZE && file_prefix <= length &&
                file_bytes <= length - file_prefix;
    int added = valid && add_locked(space, start, length, prot,
                                    VM_REGION_ELF, VM_BACKING_FILE, 0U);
    if (added)
    {
        vm_region_t* region = &space->regions[space->region_count - 1U];
        region->file_data = space->image_backing;
        region->file_offset = file_offset;
        region->file_bytes = file_bytes;
        region->file_prefix = file_prefix;
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
    if (region.backing != VM_BACKING_ZERO &&
        region.backing != VM_BACKING_FILE)
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

/* ---- Run-time mapping: mmap / munmap / brk ------------------------------ */

/* Round up to a page; returns 0 on zero input or overflow. */
static size_t round_up_page(size_t length)
{
    if (length == 0U || length > (size_t)VM_SPACE_MAX_RESERVED_BYTES)
        return 0U;
    return (length + VM_PAGE_MASK) & ~(size_t)VM_PAGE_MASK;
}

int vm_space_map_anon(vm_space_t* space, size_t length, uint32_t prot,
                      uintptr_t* address_out)
{
    const size_t rounded = round_up_page(length);
    int ok = 0;

    if (!space || !address_out || rounded == 0U)
        return 0;

    uint64_t flags = spinlock_lock_irqsave(&space->lock);

    /* Upper bound: the lowest stack/guard region above the window, else the
     * top of user space. Mappings never go above the stack reservation. */
    uint64_t high = VM_USER_LIMIT;
    for (uint32_t i = 0U; i < space->region_count; i++)
    {
        const vm_region_t* r = &space->regions[i];
        if ((r->type == VM_REGION_STACK || r->type == VM_REGION_GUARD) &&
            r->start >= VM_MMAP_WINDOW_LOW && r->start < high)
            high = r->start;
    }

    if (high > VM_MMAP_WINDOW_LOW && high - VM_MMAP_WINDOW_LOW >= rounded)
    {
        uint64_t candidate = high - rounded;
        for (;;)
        {
            const vm_region_t* hit = NULL;
            for (uint32_t i = 0U; i < space->region_count; i++)
            {
                const vm_region_t* r = &space->regions[i];
                if (candidate < r->end && r->start < candidate + rounded)
                {
                    hit = r;
                    break;
                }
            }
            if (!hit)
                break;
            /* Strictly decreasing, so this terminates. */
            if (hit->start < VM_MMAP_WINDOW_LOW + rounded)
            {
                candidate = 0U;
                break;
            }
            candidate = hit->start - rounded;
        }
        if (candidate >= VM_MMAP_WINDOW_LOW &&
            add_locked(space, (uintptr_t)candidate, rounded, prot,
                       VM_REGION_ANON, VM_BACKING_ZERO, 0U))
        {
            *address_out = (uintptr_t)candidate;
            ok = 1;
        }
    }

    spinlock_unlock_irqrestore(&space->lock, flags);
    return ok;
}

static int find_index_locked(const vm_space_t* space, uintptr_t address)
{
    for (uint32_t i = 0U; i < space->region_count; i++)
    {
        const vm_region_t* r = &space->regions[i];
        if (address >= r->start && address < r->end)
            return (int)i;
    }
    return -1;
}

static int unmappable_region(const vm_region_t* r)
{
    return r->type == VM_REGION_ANON && (r->flags & VM_REGION_FLAG_HEAP) == 0U;
}

static int unmap_range_valid(uintptr_t start, size_t length)
{
    if (length == 0U || (start & VM_PAGE_MASK) != 0U ||
        (length & VM_PAGE_MASK) != 0U || start < PAGING_USER_BASE ||
        (uint64_t)start >= VM_USER_LIMIT ||
        (uint64_t)length > VM_USER_LIMIT - (uint64_t)start)
        return 0;
    return 1;
}

int vm_space_unmap_check(vm_space_t* space, uintptr_t start, size_t length)
{
    if (!space || !unmap_range_valid(start, length))
        return 0;
    const uintptr_t end = start + length;
    int ok = 1;

    uint64_t flags = spinlock_lock_irqsave(&space->lock);
    uintptr_t cur = start;
    while (cur < end)
    {
        int idx = find_index_locked(space, cur);
        if (idx < 0 || !unmappable_region(&space->regions[idx]))
        {
            ok = 0;
            break;
        }
        const vm_region_t* r = &space->regions[idx];
        if (r->start < start && end < r->end &&
            space->region_count >= VM_REGION_MAX)
        {
            ok = 0; /* a middle punch needs one more slot */
            break;
        }
        cur = r->end < end ? r->end : end;
    }
    spinlock_unlock_irqrestore(&space->lock, flags);
    return ok;
}

int vm_space_unmap_next(vm_space_t* space, uintptr_t start, uintptr_t end,
                        vm_unmap_step_t* step)
{
    if (!space || !step || start >= end || (start & VM_PAGE_MASK) != 0U ||
        (end & VM_PAGE_MASK) != 0U)
        return 0;
    int ok = 0;

    uint64_t flags = spinlock_lock_irqsave(&space->lock);
    int idx = find_index_locked(space, start);
    if (idx >= 0 && unmappable_region(&space->regions[idx]))
    {
        const vm_region_t* r = &space->regions[idx];
        step->original = *r;
        step->start = start;
        step->end = end < r->end ? end : r->end;
        if (step->start == r->start && step->end == r->end)
            step->kind = VM_UNMAP_FULL;
        else if (step->start == r->start)
            step->kind = VM_UNMAP_FRONT;
        else if (step->end == r->end)
            step->kind = VM_UNMAP_BACK;
        else
            step->kind = VM_UNMAP_SPLIT;
        ok = 1;
    }
    spinlock_unlock_irqrestore(&space->lock, flags);
    return ok;
}

static void remove_index_locked(vm_space_t* space, uint32_t idx)
{
    /* Region order is never relied upon: fill the hole with the last entry. */
    space->region_count--;
    if (idx != space->region_count)
        space->regions[idx] = space->regions[space->region_count];
    space->regions[space->region_count] = (vm_region_t){0};
}

static void adjust_resident_locked(vm_space_t* space, uint32_t before,
                                   uint32_t after)
{
    uint32_t total = space->resident_pages;
    total = total >= before ? total - before : 0U;
    space->resident_pages = total + after;
}

int vm_space_unmap_commit(vm_space_t* space, const vm_unmap_step_t* step,
                          uint32_t left_resident, uint32_t right_resident)
{
    if (!space || !step)
        return 0;
    int ok = 0;

    uint64_t flags = spinlock_lock_irqsave(&space->lock);
    int idx = find_index_locked(space, step->original.start);
    vm_region_t* r = idx >= 0 ? &space->regions[idx] : NULL;

    if (r && r->start == step->original.start && r->end == step->original.end &&
        r->type == step->original.type && r->prot == step->original.prot &&
        unmappable_region(r) && step->start >= r->start &&
        step->end <= r->end && step->start < step->end)
    {
        const uint32_t head_pages =
            (uint32_t)((step->start - r->start) / PAGE_SIZE);
        const uint32_t tail_pages =
            (uint32_t)((r->end - step->end) / PAGE_SIZE);
        const uint64_t removed = (uint64_t)(step->end - step->start);
        const uint32_t before = r->resident_pages;

        switch (step->kind)
        {
        case VM_UNMAP_FULL:
            if (r->start == step->start && r->end == step->end)
            {
                adjust_resident_locked(space, before, 0U);
                space->reserved_bytes -= removed;
                remove_index_locked(space, (uint32_t)idx);
                ok = 1;
            }
            break;
        case VM_UNMAP_FRONT: /* tail remains */
            if (step->start == r->start && right_resident <= tail_pages)
            {
                r->start = step->end;
                r->resident_pages = right_resident;
                adjust_resident_locked(space, before, right_resident);
                space->reserved_bytes -= removed;
                ok = 1;
            }
            break;
        case VM_UNMAP_BACK: /* head remains */
            if (step->end == r->end && left_resident <= head_pages)
            {
                r->end = step->start;
                r->resident_pages = left_resident;
                adjust_resident_locked(space, before, left_resident);
                space->reserved_bytes -= removed;
                ok = 1;
            }
            break;
        case VM_UNMAP_SPLIT:
            if (step->start > r->start && step->end < r->end &&
                space->region_count < VM_REGION_MAX &&
                left_resident <= head_pages && right_resident <= tail_pages)
            {
                vm_region_t tail = *r;
                tail.start = step->end;
                tail.resident_pages = right_resident;
                r->end = step->start;
                r->resident_pages = left_resident;
                space->regions[space->region_count++] = tail;
                adjust_resident_locked(space, before,
                                       left_resident + right_resident);
                space->reserved_bytes -= removed;
                ok = 1;
            }
            break;
        default:
            break;
        }
    }
    spinlock_unlock_irqrestore(&space->lock, flags);
    return ok;
}

/* ---- brk ------------------------------------------------------------------ */

static int brk_init_locked(vm_space_t* space)
{
    if (space->brk_base != 0U)
        return 1;
    uintptr_t top = 0U;
    for (uint32_t i = 0U; i < space->region_count; i++)
    {
        const vm_region_t* r = &space->regions[i];
        if ((r->type == VM_REGION_ELF || r->type == VM_REGION_FLAT_IMAGE) &&
            r->end <= VM_MMAP_WINDOW_LOW && r->end > top)
            top = r->end;
    }
    if (top == 0U || top >= VM_MMAP_WINDOW_LOW)
        return 0;
    space->brk_base = top;
    space->brk_cur = top;
    return 1;
}

static int heap_index_locked(const vm_space_t* space)
{
    for (uint32_t i = 0U; i < space->region_count; i++)
        if ((space->regions[i].flags & VM_REGION_FLAG_HEAP) != 0U)
            return (int)i;
    return -1;
}

int vm_space_brk_get(vm_space_t* space, uintptr_t* base_out,
                     uintptr_t* current_out)
{
    if (!space)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&space->lock);
    int ok = brk_init_locked(space);
    if (ok)
    {
        if (base_out)
            *base_out = space->brk_base;
        if (current_out)
            *current_out = space->brk_cur;
    }
    spinlock_unlock_irqrestore(&space->lock, flags);
    return ok;
}

int vm_space_brk_grow(vm_space_t* space, uintptr_t requested)
{
    if (!space)
        return 0;
    int ok = 0;

    uint64_t flags = spinlock_lock_irqsave(&space->lock);
    if (brk_init_locked(space) && requested > space->brk_cur &&
        requested <= space->brk_base + VM_BRK_MAX_BYTES &&
        requested <= VM_MMAP_WINDOW_LOW)
    {
        const uintptr_t new_end =
            (requested + (uintptr_t)VM_PAGE_MASK) & ~(uintptr_t)VM_PAGE_MASK;
        int hidx = heap_index_locked(space);
        const uintptr_t cur_end = hidx >= 0 ? space->regions[hidx].end
                                            : space->brk_base;
        if (new_end == cur_end)
        {
            ok = 1; /* growth stays inside the last mapped page */
        }
        else if (hidx < 0)
        {
            ok = add_locked(space, space->brk_base, new_end - space->brk_base,
                            VM_PROT_READ | VM_PROT_WRITE, VM_REGION_ANON,
                            VM_BACKING_ZERO, VM_REGION_FLAG_HEAP);
        }
        else
        {
            const uint64_t delta = (uint64_t)(new_end - cur_end);
            if (space->reserved_bytes + delta <= VM_SPACE_MAX_RESERVED_BYTES &&
                !overlaps_locked(space, cur_end, new_end))
            {
                space->regions[hidx].end = new_end;
                space->reserved_bytes += delta;
                ok = 1;
            }
        }
        if (ok)
            space->brk_cur = requested;
    }
    spinlock_unlock_irqrestore(&space->lock, flags);
    return ok;
}

int vm_space_brk_shrink_plan(vm_space_t* space, uintptr_t requested,
                             uintptr_t* release_start, uintptr_t* release_end)
{
    if (!space || !release_start || !release_end)
        return 0;
    int ok = 0;

    uint64_t flags = spinlock_lock_irqsave(&space->lock);
    if (brk_init_locked(space) && requested >= space->brk_base &&
        requested < space->brk_cur)
    {
        int hidx = heap_index_locked(space);
        const uintptr_t new_end =
            (requested + (uintptr_t)VM_PAGE_MASK) & ~(uintptr_t)VM_PAGE_MASK;
        const uintptr_t cur_end = hidx >= 0 ? space->regions[hidx].end
                                            : space->brk_base;
        *release_start = new_end < cur_end ? new_end : cur_end;
        *release_end = cur_end;
        ok = 1;
    }
    spinlock_unlock_irqrestore(&space->lock, flags);
    return ok;
}

int vm_space_brk_shrink_commit(vm_space_t* space, uintptr_t requested,
                               uint32_t heap_resident)
{
    if (!space)
        return 0;
    int ok = 0;

    uint64_t flags = spinlock_lock_irqsave(&space->lock);
    if (brk_init_locked(space) && requested >= space->brk_base &&
        requested < space->brk_cur)
    {
        int hidx = heap_index_locked(space);
        const uintptr_t new_end =
            (requested + (uintptr_t)VM_PAGE_MASK) & ~(uintptr_t)VM_PAGE_MASK;
        if (hidx >= 0)
        {
            vm_region_t* h = &space->regions[hidx];
            const uint32_t before = h->resident_pages;
            const uint32_t pages = (uint32_t)((new_end - h->start) / PAGE_SIZE);
            if (new_end >= h->start && new_end <= h->end &&
                heap_resident <= pages)
            {
                space->reserved_bytes -= (uint64_t)(h->end - new_end);
                if (new_end == h->start)
                {
                    adjust_resident_locked(space, before, 0U);
                    remove_index_locked(space, (uint32_t)hidx);
                }
                else
                {
                    h->end = new_end;
                    h->resident_pages = heap_resident;
                    adjust_resident_locked(space, before, heap_resident);
                }
                space->brk_cur = requested;
                ok = 1;
            }
        }
    }
    spinlock_unlock_irqrestore(&space->lock, flags);
    return ok;
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
