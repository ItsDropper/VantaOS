#include "scheduler.h"
#include "cpu_local.h"
#include "process.h"
#include "paging.h"

#define SCHEDULER_QUANTUM_TICKS 5U

static int initialized;
static uint32_t fallback_quantum_ticks;

static volatile uint32_t* scheduler_quantum_slot(void)
{
    cpu_local_t* local = cpu_local_current();
    if (local && local->self == local && local->online != 0U)
        return &local->scheduler_ticks;
    return &fallback_quantum_ticks;
}

void scheduler_initialize(void)
{
    initialized = 1;
    cpu_local_t* local = cpu_local_current();
    if (local && local->self == local && local->online != 0U)
        local->scheduler_ticks = 0;
    else
        fallback_quantum_ticks = 0;
}

int scheduler_is_initialized(void)
{
    return initialized != 0;
}

static uint32_t scheduler_next_ready(uint32_t current_pid)
{
    for (uint32_t offset = 1; offset < PROCESS_MAX; offset++)
    {
        uint32_t pid = (current_pid + offset) % PROCESS_MAX;
        process_t process;
        if (process_snapshot(pid, &process) &&
            process.state == PROCESS_READY)
            return pid;
    }

    process_t idle;
    if (process_snapshot(1, &idle) &&
        (idle.state == PROCESS_READY || idle.state == PROCESS_RUNNING))
        return 1;

    return current_pid;
}

uint64_t scheduler_tick(uint64_t current_stack)
{
    if (!initialized || !process_is_initialized())
        return current_stack;

    uint32_t current_pid = process_current_pid();
    if (current_pid == 0 || current_pid >= PROCESS_MAX)
        return current_stack;

    process_save_stack(current_pid, current_stack);
    volatile uint32_t* quantum_ticks = scheduler_quantum_slot();
    if (++(*quantum_ticks) < SCHEDULER_QUANTUM_TICKS)
        return current_stack;
    *quantum_ticks = 0;

    uint32_t next_pid = scheduler_next_ready(current_pid);
    if (next_pid == current_pid)
        return current_stack;

    process_t current, next;
    if (!process_snapshot(current_pid, &current) ||
        !process_snapshot(next_pid, &next) ||
        !process_stack_is_valid(next_pid))
        return current_stack;

    if (!process_switch_to(next_pid))
        return current_stack;

    if (next.address_space != paging_get_current_address_space())
        paging_switch_address_space(next.address_space);

    if (current.state == PROCESS_TERMINATED)
        process_reap(current_pid);

    return process_get_stack(next_pid);
}

void scheduler_yield(void)
{
    uint32_t pid = process_current_pid();
    if (pid != 0)
        process_mark_ready(pid);
}
