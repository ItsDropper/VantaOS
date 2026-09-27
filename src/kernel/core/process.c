#include "process.h"

#include <stddef.h>
#include <stdint.h>

static process_t processes[PROCESS_MAX];

/*
 * Kernel thread stacks live in the statically mapped kernel image area.
 * This avoids depending on the virtual heap while the scheduler itself
 * is still bringing process execution online.
 */
static uint8_t process_stacks[PROCESS_MAX][PROCESS_STACK_SIZE]
    __attribute__((aligned(16)));

static int initialized;
static uint32_t current_pid;

static void process_copy_name(
    char* destination,
    const char* source
)
{
    unsigned int i = 0;

    while (source &&
           source[i] &&
           i < PROCESS_NAME_MAX)
    {
        destination[i] = source[i];
        i++;
    }

    destination[i] = 0;
}

static void process_reset_record(process_t* process)
{
    process->parent_pid = 0;
    process->state = PROCESS_UNUSED;
    process->name[0] = 0;
    process->stack_pointer = 0;
    process->entry = NULL;
    process->kernel_stack = NULL;
    process->address_space = NULL;

    process->context.edi = 0;
    process->context.esi = 0;
    process->context.ebp = 0;
    process->context.esp = 0;
    process->context.ebx = 0;
    process->context.edx = 0;
    process->context.ecx = 0;
    process->context.eax = 0;
    process->context.eip = 0;
    process->context.eflags = 0x202U;
    process->context.cs = 0x08U;
    process->context.ss = 0x10U;
}

static int process_allocate_common(
    process_t* process,
    const char* name,
    uint32_t parent_pid
)
{
    if (!process ||
        !name ||
        !name[0] ||
        !paging_get_kernel_address_space())
        return 0;

    process_reset_record(process);

    process->parent_pid = parent_pid;
    process->state = PROCESS_READY;
    process_copy_name(process->name, name);

    /*
     * Kernel threads execute entirely in the kernel address space.
     * Separate CR3/user mappings are not enabled until the user-memory
     * layer exists. Switching a kernel thread into an empty user
     * address space would invalidate the kernel's executable mappings.
     */
    process->address_space =
        paging_get_kernel_address_space();

    if (!process->address_space)
    {
        process_reset_record(process);
        return 0;
    }

    return 1;
}

static void process_entry_trampoline(void)
{
    uint32_t pid =
        process_current_pid();

    const process_t* process =
        process_get(pid);

    if (process && process->entry)
        process->entry();

    process_terminate_current();

    while (1)
        __asm__ volatile ("hlt");
}

static void process_prepare_stack(
    process_t* process,
    uintptr_t stack_top
)
{
    uint32_t* stack =
        (uint32_t*)stack_top;

    /*
     * This is the exact stack layout consumed by the IRQ stub:
     *
     *   popa
     *   iretd
     *
     * PUSHA saves EDI, ESI, EBP, a skipped ESP slot, EBX,
     * EDX, ECX and EAX. The CPU return frame follows it.
     */
    *(--stack) = 0x202U;
    *(--stack) = 0x08U;
    *(--stack) =
        (uint32_t)(uintptr_t)process_entry_trampoline;

    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;

    process->stack_pointer =
        (uint32_t)(uintptr_t)stack;

    process->context.esp =
        process->stack_pointer;

    process->context.eip =
        (uint32_t)(uintptr_t)process_entry_trampoline;

    process->context.eflags = 0x202U;
    process->context.cs = 0x08U;
    process->context.ss = 0x10U;
}

void process_initialize(void)
{
    initialized = 0;
    current_pid = 0;

    for (uint32_t i = 0; i < PROCESS_MAX; i++)
    {
        processes[i].pid = i;
        process_reset_record(&processes[i]);
    }

    initialized = 1;
}

int process_is_initialized(void)
{
    return initialized != 0;
}

int process_attach_current(
    const char* name,
    uint32_t parent_pid
)
{
    if (!initialized ||
        !name ||
        !name[0])
        return -1;

    for (uint32_t pid = 1; pid < PROCESS_MAX; pid++)
    {
        if (processes[pid].state != PROCESS_UNUSED &&
            processes[pid].state != PROCESS_TERMINATED)
            continue;

        process_t* process = &processes[pid];

        if (!process_allocate_common(
                process, name, parent_pid))
            return -1;

        uint32_t current_stack;

        __asm__ volatile (
            "mov %%esp, %0"
            : "=r"(current_stack)
        );

        process->state = PROCESS_RUNNING;
        process->stack_pointer = current_stack;
        process->context.esp = current_stack;
        process->context.eip =
            (uint32_t)(uintptr_t)
            __builtin_return_address(0);

        process->context.cs = 0x08U;
        process->context.ss = 0x10U;

        current_pid = pid;
        return (int)pid;
    }

    return -1;
}

int process_create_kernel(
    const char* name,
    uint32_t parent_pid,
    process_entry_t entry
)
{
    if (!initialized ||
        !name ||
        !name[0] ||
        !entry ||
        !heap_is_initialized())
        return -1;

    for (uint32_t pid = 1; pid < PROCESS_MAX; pid++)
    {
        if (processes[pid].state != PROCESS_UNUSED &&
            processes[pid].state != PROCESS_TERMINATED)
            continue;

        process_t* process = &processes[pid];

        if (!process_allocate_common(
                process, name, parent_pid))
            return -1;

        process->kernel_stack =
            process_stacks[pid];

        process->entry = entry;

        uintptr_t stack_top =
            (uintptr_t)process->kernel_stack +
            PROCESS_STACK_SIZE;

        stack_top &= ~0x0FU;

        process_prepare_stack(
            process,
            stack_top
        );

        return (int)pid;
    }

    return -1;
}

int process_mark_running(uint32_t pid)
{
    if (!initialized ||
        pid == 0 ||
        pid >= PROCESS_MAX)
        return 0;

    if (processes[pid].state == PROCESS_UNUSED ||
        processes[pid].state == PROCESS_TERMINATED)
        return 0;

    processes[pid].state = PROCESS_RUNNING;
    return 1;
}

int process_mark_ready(uint32_t pid)
{
    if (!initialized ||
        pid == 0 ||
        pid >= PROCESS_MAX)
        return 0;

    if (processes[pid].state == PROCESS_UNUSED ||
        processes[pid].state == PROCESS_TERMINATED)
        return 0;

    processes[pid].state = PROCESS_READY;
    return 1;
}

int process_set_running(uint32_t pid)
{
    if (!initialized ||
        pid >= PROCESS_MAX ||
        processes[pid].state == PROCESS_UNUSED ||
        processes[pid].state == PROCESS_TERMINATED)
        return 0;

    if (processes[current_pid].state == PROCESS_RUNNING &&
        current_pid != pid)
        processes[current_pid].state = PROCESS_READY;

    current_pid = pid;
    processes[pid].state = PROCESS_RUNNING;

    return 1;
}

int process_switch_to(uint32_t pid)
{
    if (!initialized ||
        pid == 0 ||
        pid >= PROCESS_MAX)
        return 0;

    if (processes[pid].state != PROCESS_READY &&
        processes[pid].state != PROCESS_RUNNING)
        return 0;

    if (current_pid != pid &&
        current_pid < PROCESS_MAX &&
        processes[current_pid].state == PROCESS_RUNNING)
    {
        processes[current_pid].state = PROCESS_READY;
    }

    current_pid = pid;
    processes[pid].state = PROCESS_RUNNING;

    return 1;
}

int process_wake(uint32_t pid)
{
    if (!initialized ||
        pid >= PROCESS_MAX)
        return 0;

    if (processes[pid].state != PROCESS_SLEEPING)
        return 0;

    processes[pid].state = PROCESS_READY;
    return 1;
}

void process_block_current(void)
{
    if (!initialized ||
        current_pid >= PROCESS_MAX)
        return;

    processes[current_pid].state = PROCESS_SLEEPING;
}

int process_terminate(uint32_t pid)
{
    if (!initialized ||
        pid == 0 ||
        pid >= PROCESS_MAX)
        return 0;

    process_t* process = &processes[pid];

    if (process->state == PROCESS_UNUSED ||
        process->state == PROCESS_TERMINATED)
        return 0;

    if (pid == current_pid)
    {
        process->state = PROCESS_TERMINATED;
        return 1;
    }

    process->state = PROCESS_TERMINATED;

    return process_reap(pid);
}

void process_terminate_current(void)
{
    if (!initialized ||
        current_pid == 0 ||
        current_pid >= PROCESS_MAX)
        return;

    process_t* process = &processes[current_pid];

    if (process->state == PROCESS_UNUSED)
        return;

    /*
     * A running process cannot free its own kernel stack or page
     * directory. The scheduler reaps it after switching away.
     */
    process->state = PROCESS_TERMINATED;
}

void process_save_stack(
    uint32_t pid,
    uint32_t stack_pointer
)
{
    if (!initialized ||
        pid >= PROCESS_MAX ||
        processes[pid].state == PROCESS_UNUSED)
        return;

    processes[pid].stack_pointer =
        stack_pointer;

    processes[pid].context.esp =
        stack_pointer;
}

uint32_t process_get_stack(uint32_t pid)
{
    if (!initialized ||
        pid >= PROCESS_MAX ||
        processes[pid].state == PROCESS_UNUSED)
        return 0;

    return processes[pid].stack_pointer;
}

int process_reap(uint32_t pid)
{
    if (!initialized ||
        pid == 0 ||
        pid >= PROCESS_MAX ||
        pid == current_pid)
        return 0;

    process_t* process = &processes[pid];

    if (process->state != PROCESS_TERMINATED)
        return 0;

    /*
     * Kernel processes share the kernel address space and therefore do
     * not own a page directory to destroy.
     */
    process->address_space = NULL;

    process->kernel_stack = NULL;

    process_reset_record(process);
    process->pid = pid;

    return 1;
}

uint32_t process_schedule(uint32_t current_stack)
{
    if (!initialized ||
        current_pid >= PROCESS_MAX)
        return current_stack;

    process_save_stack(
        current_pid,
        current_stack
    );

    return current_stack;
}

const process_t* process_get(uint32_t pid)
{
    if (!initialized ||
        pid >= PROCESS_MAX ||
        processes[pid].state == PROCESS_UNUSED)
        return NULL;

    return &processes[pid];
}

unsigned int process_count(void)
{
    if (!initialized)
        return 0;

    unsigned int count = 0;

    for (uint32_t i = 0; i < PROCESS_MAX; i++)
    {
        if (processes[i].state != PROCESS_UNUSED &&
            processes[i].state != PROCESS_TERMINATED)
            count++;
    }

    return count;
}

uint32_t process_current_pid(void)
{
    return current_pid;
}
