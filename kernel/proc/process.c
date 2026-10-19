/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "process_internal.h"
#include "cpu_local.h"
#include "spinlock.h"

#include <stddef.h>
#include <stdint.h>

extern unsigned char stack_bottom;
extern unsigned char stack_top;
extern unsigned char _start;
extern unsigned char _end;
extern unsigned char _kernel_text_start;
extern unsigned char _kernel_text_end;
extern void process_entry_trampoline(void);

process_t process_table_records[PROCESS_MAX];
uint8_t process_stack_storage[PROCESS_MAX][PROCESS_STACK_SIZE]
    __attribute__((aligned(16)));

int process_initialized_state;
uint32_t process_legacy_current_pid;

spinlock_t process_table_lock = SPINLOCK_INITIALIZER;

uint64_t process_table_lock_acquire(void)
{
    return spinlock_lock_irqsave(&process_table_lock);
}

void process_table_lock_release(uint64_t flags)
{
    spinlock_unlock_irqrestore(&process_table_lock, flags);
}

cpu_local_t* process_local_state(void)
{
    cpu_local_t* local = cpu_local_current();
    if (!local || local->self != local || local->online == 0U)
        return NULL;
    return local;
}

uint32_t process_current_cpu_id(void)
{
    cpu_local_t* local = process_local_state();
    return local ? local->logical_id : 0U;
}

uint32_t process_current_slot(void)
{
    cpu_local_t* local = process_local_state();
    return local ? local->current_pid : process_legacy_current_pid;
}

void process_set_current_slot(uint32_t pid)
{
    cpu_local_t* local = process_local_state();
    if (local)
        local->current_pid = pid;
    else
        process_legacy_current_pid = pid;
}

/* Caller must hold process_table_lock. Idle PIDs are persistent CPU-local
 * references and must never be terminated, reaped, or reused as ordinary work. */
int process_is_idle_locked(uint32_t pid)
{
    if (pid == 0U || pid >= PROCESS_MAX)
        return 0;

    for (uint32_t cpu = 0; cpu < cpu_local_capacity(); cpu++)
    {
        const cpu_local_t* local = cpu_local_get(cpu);
        if (local &&
            __atomic_load_n(&local->idle_pid, __ATOMIC_ACQUIRE) == pid)
            return 1;
    }
    return 0;
}

static void process_copy_name(char* destination, const char* source)
{
    unsigned int i = 0;
    while (source && i < PROCESS_NAME_MAX && source[i])
    {
        destination[i] = source[i];
        i++;
    }
    destination[i] = 0;
}

void process_reset_record(process_t* process)
{
    process->parent_pid = 0;
    process->owner_cpu = PROCESS_CPU_UNOWNED;
    process->state = PROCESS_UNUSED;
    process->exit_status = 0;
    process->name[0] = 0;
    process->stack_pointer = 0;
    process->entry = NULL;
    process->kernel_stack = NULL;
    process->address_space = NULL;
    process->is_user = 0U;
    process->user_page_count = 0U;
    process->user_entry = 0U;
    process->user_stack_base = 0U;
    process->user_stack_top = 0U;
    process->user_image_size = 0U;
    for (uint32_t i = 0; i < PROCESS_FD_COUNT; i++)
        process->file_descriptors[i] = -1;
    for (uint32_t i = 0; i < PROCESS_USER_MAX_PAGES; i++)
    {
        process->user_pages[i].virtual_address = 0U;
        process->user_pages[i].physical_frame = NULL;
        process->user_pages[i].flags = 0U;
    }

    process->context.r15 = 0;
    process->context.r14 = 0;
    process->context.r13 = 0;
    process->context.r12 = 0;
    process->context.r11 = 0;
    process->context.r10 = 0;
    process->context.r9 = 0;
    process->context.r8 = 0;
    process->context.rsi = 0;
    process->context.rdi = 0;
    process->context.rbp = 0;
    process->context.rdx = 0;
    process->context.rcx = 0;
    process->context.rbx = 0;
    process->context.rax = 0;
    process->context.rsp = 0;
    process->context.rip = 0;
    process->context.rflags = 0x202U;
    process->context.cs = 0x08U;
    process->context.ss = 0x10U;
}

/* Caller must hold process_table_lock. */
int process_stack_is_valid_locked(const process_t* process)
{
    if (!process || process->state == PROCESS_UNUSED ||
        process->stack_pointer == 0 || process->kernel_stack == NULL)
        return 0;

    uintptr_t stack_base = (uintptr_t)process->kernel_stack;
    uintptr_t stack_end =
        process->kernel_stack == (void*)(uintptr_t)&stack_bottom ?
        (uintptr_t)&stack_top : stack_base + PROCESS_STACK_SIZE;
    uintptr_t frame = (uintptr_t)process->stack_pointer;

    /* Every interrupt frame has RIP, CS and RFLAGS. A user task can be
       preempted while executing either ring-3 code or a ring-0 syscall.
       The saved frame's CS, not the process type, determines its shape. */
    if (frame < stack_base || frame > stack_end ||
        stack_end - frame < 19U * sizeof(uint64_t))
        return 0;

    const uint64_t* values = (const uint64_t*)frame;
    uintptr_t rip = (uintptr_t)values[16];
    uint64_t code_selector = values[17];
    if ((values[18] & 0x00000200U) == 0)
        return 0;

    if (code_selector == 0x08U)
    {
        uintptr_t code_start = (uintptr_t)&_kernel_text_start;
        uintptr_t code_end = (uintptr_t)&_kernel_text_end;
        return rip >= code_start && rip < code_end;
    }

    if (code_selector != 0x23U || !process->is_user ||
        stack_end - frame < 21U * sizeof(uint64_t) ||
        values[20] != 0x1BU ||
        process->user_page_count == 0U ||
        process->user_page_count > PROCESS_USER_MAX_PAGES ||
        values[19] < process->user_stack_base ||
        values[19] > process->user_stack_top)
        return 0;

    int executable_page = 0;
    for (uint32_t i = 0; i < process->user_page_count; i++)
    {
        const process_user_page_t* page = &process->user_pages[i];
        if ((page->flags & PROCESS_USER_PAGE_EXEC) != 0U &&
            rip >= page->virtual_address &&
            rip - page->virtual_address < PAGE_SIZE)
        {
            executable_page = 1;
            break;
        }
    }
    return executable_page;
}

static int process_allocate_common(process_t* process,
                                    const char* name,
                                    uint32_t parent_pid)
{
    if (!process || !name || !name[0] ||
        !paging_get_kernel_address_space())
        return 0;

    process_reset_record(process);
    process->parent_pid = parent_pid;
    process->state = PROCESS_READY;
    process->affinity_cpu = process_current_cpu_id();
    process_copy_name(process->name, name);
    process->address_space = paging_get_kernel_address_space();

    return process->address_space != NULL;
}

void process_entry_dispatch(void)
{
    uint32_t pid = process_current_pid();
    process_t process;
    if (process_snapshot(pid, &process) && process.entry)
        process.entry();

    process_terminate_current();
}

static void process_prepare_stack(process_t* process, uintptr_t stack_top)
{
    uint64_t* stack = (uint64_t*)stack_top;

    /*
     * Initial context layout consumed by RESTORE_ALL followed by IRETQ.
     * This ring-0 IRETQ consumes RIP, CS, and RFLAGS. RSP and SS are
     * reserved words at the top of this synthetic initial stack.
     *
     * Offsets: padding 0; GPRs +8..+120; RIP +128; CS +136;
     * RFLAGS +144; reserved RSP +152; reserved SS +160.
     */
    *(--stack) = 0x10U; /* SS */
    *(--stack) = (uint64_t)stack_top; /* RSP after IRETQ */
    *(--stack) = 0x202U; /* RFLAGS: IF enabled */
    *(--stack) = 0x08U; /* CS */
    *(--stack) = (uint64_t)(uintptr_t)process_entry_trampoline; /* RIP */

    for (int i = 0; i < 15; i++)
        *(--stack) = 0;

    *(--stack) = 0; /* IRQ C-call alignment padding */

    process->stack_pointer = (uint64_t)(uintptr_t)stack;
    process->context.rsp = process->stack_pointer;
    process->context.rip = (uint64_t)(uintptr_t)process_entry_trampoline;
    process->context.rflags = 0x202U;
    process->context.cs = 0x08U;
    process->context.ss = 0x10U;
}

void process_initialize(void)
{
    __atomic_store_n(&process_initialized_state, 0, __ATOMIC_RELAXED);
    process_legacy_current_pid = 0;
    cpu_local_t* local = process_local_state();
    if (local)
        local->current_pid = 0;

    for (uint32_t i = 0; i < PROCESS_MAX; i++)
    {
        process_table_records[i].pid = i;
        process_reset_record(&process_table_records[i]);
    }

    __atomic_store_n(&process_initialized_state, 1, __ATOMIC_RELEASE);
}

int process_is_initialized(void)
{
    return __atomic_load_n(&process_initialized_state, __ATOMIC_ACQUIRE) != 0;
}

int process_attach_current(const char* name, uint32_t parent_pid)
{
    if (!process_initialized_state || !name || !name[0])
        return -1;

    uint64_t flags = process_table_lock_acquire();
    for (uint32_t pid = 1; pid < PROCESS_MAX; pid++)
    {
        if (process_table_records[pid].state != PROCESS_UNUSED ||
            process_is_idle_locked(pid))
            continue;

        process_t* process = &process_table_records[pid];
        if (!process_allocate_common(process, name, parent_pid))
        {
            process_table_lock_release(flags);
            return -1;
        }

        uint64_t current_stack;
        __asm__ volatile ("mov %%rsp, %0" : "=r"(current_stack));
        process->state = PROCESS_RUNNING;
        process->owner_cpu = process_current_cpu_id();
        process->stack_pointer = current_stack;
        process->context.rsp = current_stack;
        process->context.rip =
            (uint64_t)(uintptr_t)__builtin_return_address(0);
        process->context.cs = 0x08U;
        process->context.ss = 0x10U;
        process->kernel_stack = (void*)(uintptr_t)&stack_bottom;
        process_set_current_slot(pid);
        process_table_lock_release(flags);
        return (int)pid;
    }

    process_table_lock_release(flags);
    return -1;
}

static int process_create_kernel_affinity(const char* name,
                                          uint32_t parent_pid,
                                          process_entry_t entry,
                                          uint32_t affinity_cpu,
                                          cpu_local_t* idle_local)
{
    if (!process_initialized_state || !name || !name[0] || !entry)
        return -1;

    uint64_t flags = process_table_lock_acquire();
    if (idle_local &&
        (idle_local->logical_id != affinity_cpu ||
         idle_local->online == 0U ||
         __atomic_load_n(&idle_local->idle_pid, __ATOMIC_ACQUIRE) != 0U))
    {
        process_table_lock_release(flags);
        return -1;
    }

    for (uint32_t pid = 1; pid < PROCESS_MAX; pid++)
    {
        if (process_table_records[pid].state != PROCESS_UNUSED ||
            process_is_idle_locked(pid))
            continue;

        process_t* process = &process_table_records[pid];
        if (!process_allocate_common(process, name, parent_pid))
        {
            process_table_lock_release(flags);
            return -1;
        }

        process->affinity_cpu = affinity_cpu;
        process->kernel_stack = process_stack_storage[pid];
        process->entry = entry;
        uintptr_t stack_top =
            (uintptr_t)process->kernel_stack + PROCESS_STACK_SIZE;
        stack_top &= ~(uintptr_t)0x0FU;
        process_prepare_stack(process, stack_top);
        if (idle_local)
            __atomic_store_n(&idle_local->idle_pid, pid, __ATOMIC_RELEASE);
        process_table_lock_release(flags);
        return (int)pid;
    }

    process_table_lock_release(flags);
    return -1;
}


int process_create_kernel(const char* name, uint32_t parent_pid,
                          process_entry_t entry)
{
    return process_create_kernel_affinity(name, parent_pid, entry,
                                          process_current_cpu_id(), NULL);
}

int process_create_kernel_any(const char* name, uint32_t parent_pid,
                              process_entry_t entry)
{
    return process_create_kernel_affinity(name, parent_pid, entry,
                                          PROCESS_CPU_ANY, NULL);
}



int process_create_kernel_on_cpu(const char* name, uint32_t parent_pid,
                                  process_entry_t entry, uint32_t cpu_id)
{
    if (cpu_id >= CPU_LOCAL_MAX || !cpu_local_get(cpu_id))
        return -1;
    return process_create_kernel_affinity(name, parent_pid, entry, cpu_id, NULL);
}


int process_create_idle_for_cpu(uint32_t cpu_id, const char* name,
                                process_entry_t entry)
{
    if (!process_initialized_state || cpu_id >= CPU_LOCAL_MAX || !name || !name[0] ||
        !entry)
        return 0;

    cpu_local_t* local = (cpu_local_t*)cpu_local_get(cpu_id);
    if (!local || local->self != local || local->online == 0U)
        return 0;

    int pid = process_create_kernel_affinity(name, 0U, entry,
                                                cpu_id, local);
    if (pid < 0)
        return 0;

    return pid;
}


int process_fd_install(uint32_t pid, int vfs_handle)
{
    if (!process_initialized_state || pid >= PROCESS_MAX || vfs_handle < 0)
        return -1;

    uint64_t flags = process_table_lock_acquire();
    process_t* process = &process_table_records[pid];
    if (process->state == PROCESS_UNUSED ||
        process->state == PROCESS_TERMINATED ||
        process->state == PROCESS_REAPING || !process->is_user)
    {
        process_table_lock_release(flags);
        return -1;
    }

    for (uint32_t i = 0; i < PROCESS_FD_COUNT; i++)
    {
        if (process->file_descriptors[i] >= 0)
            continue;
        process->file_descriptors[i] = vfs_handle;
        process_table_lock_release(flags);
        return PROCESS_FD_BASE + (int)i;
    }

    process_table_lock_release(flags);
    return -1;
}

int process_fd_get(uint32_t pid, int descriptor)
{
    if (!process_initialized_state || pid >= PROCESS_MAX ||
        descriptor < PROCESS_FD_BASE ||
        descriptor >= PROCESS_FD_BASE + (int)PROCESS_FD_COUNT)
        return -1;

    uint64_t flags = process_table_lock_acquire();
    process_t* process = &process_table_records[pid];
    int handle = -1;
    if (process->state != PROCESS_UNUSED &&
        process->state != PROCESS_TERMINATED &&
        process->state != PROCESS_REAPING && process->is_user)
    {
        handle = process->file_descriptors[
            (uint32_t)(descriptor - PROCESS_FD_BASE)];
    }
    process_table_lock_release(flags);
    return handle;
}

int process_fd_remove(uint32_t pid, int descriptor, int* vfs_handle_out)
{
    if (!process_initialized_state || pid >= PROCESS_MAX || !vfs_handle_out ||
        descriptor < PROCESS_FD_BASE ||
        descriptor >= PROCESS_FD_BASE + (int)PROCESS_FD_COUNT)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    process_t* process = &process_table_records[pid];
    uint32_t index = (uint32_t)(descriptor - PROCESS_FD_BASE);
    if (process->state == PROCESS_UNUSED || !process->is_user ||
        process->file_descriptors[index] < 0)
    {
        process_table_lock_release(flags);
        return 0;
    }

    *vfs_handle_out = process->file_descriptors[index];
    process->file_descriptors[index] = -1;
    process_table_lock_release(flags);
    return 1;
}
