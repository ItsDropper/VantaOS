/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "syscall_vm.h"

#include "vm_region.h"

/* Keep in step with userspace/include/vanta/syscall.h. */
#define SYS_VM_PROT_READ  0x1U
#define SYS_VM_PROT_WRITE 0x2U
#define SYS_VM_PROT_MASK  0x3U

#define SYS_VM_EINVAL 22
#define SYS_VM_ENOMEM 12
#define SYS_VM_EIO     5

#ifdef VANTAOS_HOST_TEST
#define VM_LOG(msg) ((void)(msg))
#else
#include "serial.h"
#define VM_LOG(msg) serial_write(msg)
#endif

static int64_t fail(int errno_value)
{
    return -(int64_t)errno_value;
}

int64_t syscall_vm_mmap(paging_address_space_t* space, uint64_t length,
                        uint64_t prot, uint64_t flags)
{
    if (!space)
        return fail(SYS_VM_EINVAL);
    /* Anonymous private memory only: no fd, no address hint, no flags yet. */
    if (flags != 0U || length == 0U || (prot & ~(uint64_t)SYS_VM_PROT_MASK) != 0U ||
        prot == 0U)
        return fail(SYS_VM_EINVAL);
    if (length > VM_SPACE_MAX_RESERVED_BYTES)
        return fail(SYS_VM_ENOMEM);

    /* x86 cannot express write-only pages; PROT_WRITE implies readable. */
    uint32_t vm_prot = VM_PROT_READ;
    if ((prot & SYS_VM_PROT_WRITE) != 0U)
        vm_prot |= VM_PROT_WRITE;

    uintptr_t address = 0U;
    if (!vm_space_map_anon(&space->vm, (size_t)length, vm_prot, &address))
        return fail(SYS_VM_ENOMEM);
    return (int64_t)address;
}

int64_t syscall_vm_munmap(paging_address_space_t* space, uint64_t address,
                          uint64_t length)
{
    const uint64_t page = PAGE_SIZE;
    if (!space || length == 0U || (address & (page - 1U)) != 0U ||
        length > VM_SPACE_MAX_RESERVED_BYTES)
        return fail(SYS_VM_EINVAL);

    const uint64_t rounded = (length + page - 1U) & ~(page - 1U);
    if (!vm_space_unmap_check(&space->vm, (uintptr_t)address, (size_t)rounded))
        return fail(SYS_VM_EINVAL);

    const uintptr_t end = (uintptr_t)(address + rounded);
    uintptr_t cursor = (uintptr_t)address;
    while (cursor < end)
    {
        vm_unmap_step_t step;
        uint32_t head = 0U, tail = 0U, freed = 0U;

        if (!vm_space_unmap_next(&space->vm, cursor, end, &step) ||
            !paging_release_user_demand_range(
                space, step.original.start, step.start, step.end,
                step.original.end, &head, &freed, &tail))
        {
            /* Nothing was changed for this piece; earlier pieces were. */
            VM_LOG("VANTAOS_MUNMAP_INCONSISTENT release\n");
            return fail(cursor == (uintptr_t)address ? SYS_VM_EINVAL : SYS_VM_EIO);
        }
        if (!vm_space_unmap_commit(&space->vm, &step, head, tail))
        {
            /* Frames are already freed, so the table must not be left stale. */
            VM_LOG("VANTAOS_MUNMAP_INCONSISTENT commit\n");
            return fail(SYS_VM_EIO);
        }
        cursor = step.end;
    }
    return 0;
}

int64_t syscall_vm_brk(paging_address_space_t* space, uint64_t requested)
{
    uintptr_t base = 0U, current = 0U;

    if (!space)
        return fail(SYS_VM_EINVAL);
    /* No loadable image to anchor a heap to. */
    if (!vm_space_brk_get(&space->vm, &base, &current))
        return fail(SYS_VM_ENOMEM);
    /* Query, no-op and every refused request report the current break. */
    if (requested == 0U || requested == current || requested < base)
        return (int64_t)current;

    if (requested > current)
        return vm_space_brk_grow(&space->vm, (uintptr_t)requested)
                   ? (int64_t)requested
                   : (int64_t)current;

    uintptr_t release_start = 0U, release_end = 0U;
    if (!vm_space_brk_shrink_plan(&space->vm, (uintptr_t)requested,
                                  &release_start, &release_end))
        return (int64_t)current;

    uint32_t heap_resident = 0U;
    if (release_start < release_end)
    {
        uint32_t freed = 0U, unused = 0U;
        if (!paging_release_user_demand_range(space, base, release_start,
                                              release_end, release_end,
                                              &heap_resident, &freed, &unused))
            return (int64_t)current;
    }
    else
    {
        vm_region_t heap;
        if (!vm_space_find_region(&space->vm, base, &heap))
            return (int64_t)current;
        heap_resident = heap.resident_pages;
    }

    if (!vm_space_brk_shrink_commit(&space->vm, (uintptr_t)requested,
                                    heap_resident))
    {
        VM_LOG("VANTAOS_BRK_INCONSISTENT commit\n");
        return fail(SYS_VM_EIO);
    }
    return (int64_t)requested;
}
