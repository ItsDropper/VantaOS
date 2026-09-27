#include "process.h"
#include "heap.h"

#include <stddef.h>
#include <stdint.h>

static process_t processes[PROCESS_MAX];

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

    process->address_space =
        paging_create_address_space();

    if (!process->address_space)
    {
        process_reset_record(process);
        return 0;
    }

    return 1;
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

        /*
         * The desktop already exists on the kernel's current stack.
         * Capture that stack pointer instead of fabricating a new
         * execution context.
         */
        uint32_t current_stack;

        __asm__ volatile (
            "mov %%esp, %0"
            : "=r"(current_stack)
        );

        process->state = PROCESS_RUNNING;
        process->stack_pointer = current_stack;
        process->context.esp = current_stack;
        process->context.eip = 0;
        process->context.eip =
            (uint32_t)(uintptr_t)__builtin_return_address(0);

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
            kmalloc(PROCESS_STACK_SIZE);

        if (!process->kernel_stack)
        {
            paging_destroy_address_space(
                process->address_space
            );
            process_reset_record(process);
            return -1;
        }

        process->entry = entry;

        uintptr_t stack_top =
            (uintptr_t)process->kernel_stack +
            PROCESS_STACK_SIZE;

        stack_top &= ~0x0FU;

        process->stack_pointer =
            (uint32_t)stack_top;

        /*
         * This is a prepared kernel execution context. It is not
         * executed yet; process_schedule() remains deliberately
         * non-switching until the assembly context-switch path is
         * introduced and verified separately.
         */
        process->context.esp = (uint32_t)stack_top;
        process->context.eip =
            (uint32_t)(uintptr_t)entry;
        process->context.eflags = 0x202U;
        process->context.cs = 0x08U;
        process->context.ss = 0x10U;

        return (int)pid;
    }

    return -1;
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

    process->state = PROCESS_TERMINATED;

    if (process->address_space)
    {
        paging_destroy_address_space(
            process->address_space
        );
        process->address_space = NULL;
    }

    if (process->kernel_stack)
    {
        kfree(process->kernel_stack);
        process->kernel_stack = NULL;
    }

    process->stack_pointer = 0;
    process->entry = NULL;
    process->context.esp = 0;
    process->context.eip = 0;

    return 1;
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

    process->state = PROCESS_TERMINATED;

    if (process->address_space)
    {
        paging_destroy_address_space(
            process->address_space
        );
        process->address_space = NULL;
    }

    if (process->kernel_stack)
    {
        kfree(process->kernel_stack);
        process->kernel_stack = NULL;
    }

    process->stack_pointer = 0;
    process->entry = NULL;
}

uint32_t process_schedule(uint32_t current_stack)
{
    /*
     * Scheduling metadata is now real, including per-process kernel
     * stacks and address spaces. Actual CPU context switching is kept
     * disabled until the dedicated assembly switch path is added.
     */
    if (!initialized ||
        current_pid >= PROCESS_MAX)
        return current_stack;

    processes[current_pid].stack_pointer = current_stack;
    processes[current_pid].context.esp = current_stack;

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
