/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "process_internal.h"
#include "pmm.h"
#include "serial.h"
#include "vfs.h"

#include <stddef.h>
#include <stdint.h>

#define USER_CODE_ADDRESS ((uintptr_t)PAGING_USER_BASE)
#define USER_STACK_ADDRESS ((uintptr_t)(PAGING_USER_BASE + PAGE_SIZE))
#define USER_STACK_TOP ((uintptr_t)(PAGING_USER_BASE + 2U * PAGE_SIZE))
#define IDENTITY_MAP_LIMIT 0x40000000ULL
#define USER_INITIAL_STACK_WORDS 6U

static void copy_image(void* destination, const void* source, size_t size)
{
    uint8_t* out = (uint8_t*)destination;
    const uint8_t* in = (const uint8_t*)source;
    for (size_t i = 0; i < size; i++)
        out[i] = in[i];
}

static uintptr_t initialize_initial_stack(void* stack_frame,
                                          uintptr_t stack_top)
{
    uint64_t* words = (uint64_t*)((uint8_t*)stack_frame +
        PAGE_SIZE - USER_INITIAL_STACK_WORDS * sizeof(uint64_t));
    /* argc=0, argv[0]=NULL, envp[0]=NULL, AT_NULL=(0,0). */
    for (uint32_t i = 0; i < USER_INITIAL_STACK_WORDS; i++)
        words[i] = 0U;
    return stack_top - USER_INITIAL_STACK_WORDS * sizeof(uint64_t);
}

static void prepare_user_context(process_t* process, uintptr_t kernel_stack_top,
                                 uintptr_t entry, uintptr_t user_stack_pointer)
{
    uint64_t* stack = (uint64_t*)kernel_stack_top;
    *(--stack) = 0x1BU;
    *(--stack) = user_stack_pointer;
    *(--stack) = 0x202U;
    *(--stack) = 0x23U;
    *(--stack) = entry;
    for (unsigned int i = 0; i < 15U; i++)
        *(--stack) = 0U;
    *(--stack) = 0U;

    process->stack_pointer = (uintptr_t)stack;
    process->context.rsp = (uintptr_t)stack;
    process->context.rip = entry;
    process->context.rflags = 0x202U;
    process->context.cs = 0x23U;
    process->context.ss = 0x1BU;
}

int process_install_user_image(const char* name, uint32_t parent_pid,
                               paging_address_space_t* space,
                               uintptr_t entry, uintptr_t stack_base,
                               uintptr_t stack_top, uintptr_t initial_stack,
                               size_t image_size,
                               const process_user_page_t* pages,
                               uint32_t page_count)
{
    if (!process_initialized_state || !name || !name[0] || !space ||
        !pages || page_count == 0U ||
        page_count > PROCESS_USER_MAX_PAGES ||
        entry < PAGING_USER_BASE || entry > PAGING_USER_END ||
        stack_base < PAGING_USER_BASE || stack_top <= stack_base ||
        stack_top - stack_base > PROCESS_USER_MAX_PAGES * PAGE_SIZE ||
        initial_stack < stack_base || initial_stack > stack_top ||
        (initial_stack & 0xFU) != 0U)
        return -1;

    int entry_executable = 0;
    for (uint32_t i = 0; i < page_count; i++)
    {
        if ((pages[i].virtual_address & (PAGE_SIZE - 1U)) != 0U ||
            !pages[i].physical_frame)
            return -1;
        if ((pages[i].flags & PROCESS_USER_PAGE_EXEC) != 0U &&
            entry >= pages[i].virtual_address &&
            entry - pages[i].virtual_address < PAGE_SIZE)
            entry_executable = 1;
    }
    if (!entry_executable ||
        !paging_user_range_valid(space, initial_stack, 1U, 1))
        return -1;

    uint64_t flags = process_table_lock_acquire();
    for (uint32_t pid = 1U; pid < PROCESS_MAX; pid++)
    {
        process_t* process = &process_table_records[pid];
        if (process->state != PROCESS_UNUSED || process_is_idle_locked(pid))
            continue;

        process_reset_record(process);
        process->parent_pid = parent_pid;
        process->owner_cpu = PROCESS_CPU_UNOWNED;
        process->affinity_cpu = process_current_cpu_id();
        process->state = PROCESS_READY;
        process->is_user = 1U;
        process->user_page_count = page_count;
        process->user_entry = entry;
        process->user_stack_base = stack_base;
        process->user_stack_top = stack_top;
        process->user_image_size = image_size;
        process->address_space = space;
        process->kernel_stack = process_stack_storage[pid];

        for (uint32_t i = 0; i < page_count; i++)
            process->user_pages[i] = pages[i];

        unsigned int i = 0;
        while (i < PROCESS_NAME_MAX && name[i])
        {
            process->name[i] = name[i];
            i++;
        }
        process->name[i] = '\0';

        uintptr_t kernel_stack_top =
            ((uintptr_t)process->kernel_stack + PROCESS_STACK_SIZE) &
            ~(uintptr_t)0x0FU;
        prepare_user_context(process, kernel_stack_top, entry,
                             initial_stack);
        process_table_lock_release(flags);
        return (int)pid;
    }

    process_table_lock_release(flags);
    return -1;
}

void process_release_unpublished_user_image(
    paging_address_space_t* space, void* code_frame, void* stack_frame)
{
    /*
     * User frames may still be reachable through the address space if teardown
     * is refused (for example, while a TLB shootdown is outstanding). Never
     * return those frames to PMM unless the page tables were actually retired.
     * A bounded leak is safer than reusing memory still mapped by a CPU.
     */
    if (space && !paging_destroy_address_space(space))
        return;
    if (code_frame)
        pmm_free_block(code_frame);
    if (stack_frame)
        pmm_free_block(stack_frame);
}

int process_create_user(const char* name, uint32_t parent_pid,
                        const void* image, size_t image_size)
{
    if (!process_initialized_state || !name || !name[0] || !image ||
        image_size == 0U || image_size > PAGE_SIZE)
        return -1;

    paging_address_space_t* space = paging_create_address_space();
    void* code_frame = pmm_alloc_block();
    void* stack_frame = pmm_alloc_block();
    if (!space || !code_frame || !stack_frame ||
        (uintptr_t)code_frame >= IDENTITY_MAP_LIMIT ||
        (uintptr_t)stack_frame >= IDENTITY_MAP_LIMIT)
    {
        process_release_unpublished_user_image(space, code_frame, stack_frame);
        return -1;
    }

    copy_image(code_frame, image, image_size);
    for (size_t i = 0; i < PAGE_SIZE; i++)
        ((uint8_t*)stack_frame)[i] = 0U;
    uintptr_t initial_stack = initialize_initial_stack(
        stack_frame, USER_STACK_TOP);

    if (!paging_map_user_page_ex(space, USER_CODE_ADDRESS,
                                 (uintptr_t)code_frame, 0, 1) ||
        !paging_map_user_page_ex(space, USER_STACK_ADDRESS,
                                 (uintptr_t)stack_frame, 1, 0))
    {
        process_release_unpublished_user_image(space, code_frame, stack_frame);
        return -1;
    }

    process_user_page_t pages[2] = {
        { USER_CODE_ADDRESS, code_frame, PROCESS_USER_PAGE_EXEC },
        { USER_STACK_ADDRESS, stack_frame, PROCESS_USER_PAGE_WRITE }
    };
    int pid = process_install_user_image(
        name, parent_pid, space, USER_CODE_ADDRESS,
        USER_STACK_ADDRESS, USER_STACK_TOP, initial_stack,
        image_size, pages, 2U);
    if (pid <= 0)
    {
        process_release_unpublished_user_image(space, code_frame, stack_frame);
        return -1;
    }
    return pid;
}

int process_release_user_resources(process_t* process)
{
    if (!process)
        return 0;
    if (!process->is_user)
        return 1;
    if (!process->address_space ||
        process->user_page_count == 0U ||
        process->user_page_count > PROCESS_USER_MAX_PAGES ||
        paging_get_current_address_space() == process->address_space)
        return 0;

    if (!paging_destroy_address_space(process->address_space))
        return 0;

    for (uint32_t i = 0; i < PROCESS_FD_COUNT; i++)
        if (process->file_descriptors[i] >= 0)
            (void)vfs_close(process->file_descriptors[i]);

    for (uint32_t i = 0; i < process->user_page_count; i++)
    {
        if (process->user_pages[i].physical_frame)
            pmm_free_block(process->user_pages[i].physical_frame);
    }

    process->address_space = NULL;
    process->user_page_count = 0U;
    process->user_entry = 0U;
    process->user_stack_base = 0U;
    process->user_stack_top = 0U;
    process->user_image_size = 0U;
    process->is_user = 0U;
    serial_write("VANTAOS_USER_PROCESS_REAP_OK\n");
    return 1;
}
