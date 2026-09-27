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
    for (uint32_t offset = 1; offset < PROCESS_MAX; offset++)
    {
        uint32_t pid =
            (current_pid + offset) % PROCESS_MAX;

        const process_t* process =
            process_get(pid);

        if (process &&
            process->state == PROCESS_READY)
            return pid;
    }

    return current_pid;
}

uint32_t scheduler_tick(uint32_t current_stack)
{
    if (!initialized ||
        !process_is_initialized())
        return current_stack;

    uint32_t current_pid =
        process_current_pid();

    if (current_pid >= PROCESS_MAX)
        return current_stack;

    /*
     * PID 0 is the boot context. It has no process record and therefore
     * never has a scheduler-owned stack to save.
     */
    if (current_pid != 0)
        process_save_stack(current_pid, current_stack);

    quantum_ticks++;

    /*
     * The first timer interrupt is the bootstrap point from the boot
     * context into the first real kernel process.
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

    if (!next ||
        !process_stack_is_valid(next_pid))
        return current_stack;

    if (!process_switch_to(next_pid))
        return current_stack;

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
    uint32_t pid = process_current_pid();

    if (pid != 0)
        process_mark_ready(pid);
}
