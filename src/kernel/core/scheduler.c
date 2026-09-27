#include "scheduler.h"
#include "process.h"
#include "paging.h"

#define SCHEDULER_QUANTUM_TICKS 5U

static int initialized;
static uint32_t quantum_ticks;

void scheduler_initialize(void)
{
    initialized = 1;
    quantum_ticks = 0;
}

int scheduler_is_initialized(void)
{
    return initialized != 0;
}

static uint32_t scheduler_next_ready(uint32_t current_pid)
{
    /*
     * PID 1 is the boot/idle context. It owns the original kernel stack
     * and must never be selected while another runnable kernel process
     * exists. Real processes always get their own managed stack.
     */
    for (uint32_t offset = 1; offset < PROCESS_MAX; offset++)
    {
        uint32_t pid =
            (current_pid + offset) % PROCESS_MAX;

        if (pid == 1)
            continue;

        const process_t* process =
            process_get(pid);

        if (process && process->state == PROCESS_READY)
            return pid;
    }

    return current_pid;
}

uint32_t scheduler_tick(uint32_t current_stack)
{
    if (!initialized || !process_is_initialized())
        return current_stack;

    uint32_t current_pid =
        process_current_pid();

    if (current_pid >= PROCESS_MAX)
        return current_stack;

    /*
     * PID 0 is the boot context, not a process. It has no scheduler-owned
     * stack frame and must never be restored through IRET.
     */
    if (current_pid != 0)
    {
        process_save_stack(
            current_pid,
            current_stack
        );
    }

    quantum_ticks++;

    /*
     * The first real process is entered directly from the boot context.
     * There is no reason to spend five timer ticks returning to a stack
     * that belongs to kernel_main/_start.
     */
    if (current_pid == 0)
        quantum_ticks = SCHEDULER_QUANTUM_TICKS;

    if (quantum_ticks < SCHEDULER_QUANTUM_TICKS)
        return current_stack;

    quantum_ticks = 0;

    uint32_t next_pid =
        scheduler_next_ready(current_pid);

    if (next_pid == current_pid)
        return current_stack;

    const process_t* next =
        process_get(next_pid);

    if (!next)
        return current_stack;

    if (!process_stack_is_valid(next_pid))
        return current_stack;

    if (!process_switch_to(next_pid))
        return current_stack;

    /*
     * Kernel threads currently share the kernel address space.
     * Do not load a per-process CR3 until user address spaces are real.
     */
    if (next->address_space !=
        paging_get_current_address_space())
    {
        paging_switch_address_space(
            next->address_space
        );
    }

    if (current_pid != 0)
    {
        const process_t* current =
            process_get(current_pid);

        if (current &&
            current->state == PROCESS_TERMINATED)
            process_reap(current_pid);
    }

    return process_get_stack(next_pid);
}

void scheduler_yield(void)
{
    /*
     * Kernel code cannot safely change stacks synchronously from C.
     * Marking the task ready lets the next timer interrupt perform the
     * actual register and address-space switch.
     */
    uint32_t pid = process_current_pid();

    if (pid != 0)
        process_mark_ready(pid);
}
