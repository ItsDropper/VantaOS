#include "scheduler.h"
#include "cpu_local.h"
#include "process.h"
#include "paging.h"
#include "panic.h"

#define SCHEDULER_QUANTUM_TICKS 5U

static int initialized;
static uint32_t fallback_quantum_ticks;
/* PID being switched away from; consumed after interrupt assembly changes RSP. */
static volatile uint32_t pending_outgoing_pid[CPU_LOCAL_MAX];

static volatile uint32_t* scheduler_quantum_slot(void)
{
    cpu_local_t* local = cpu_local_current();
    if (local && local->self == local && local->online != 0U)
        return &local->scheduler_ticks;
    return &fallback_quantum_ticks;
}

void scheduler_initialize(void)
{
    cpu_local_t* local = cpu_local_current();
    if (local && local->self == local && local->online != 0U)
        local->scheduler_ticks = 0;
    else
        fallback_quantum_ticks = 0;
    __atomic_store_n(&initialized, 1, __ATOMIC_RELEASE);
}

int scheduler_is_initialized(void)
{
    return __atomic_load_n(&initialized, __ATOMIC_ACQUIRE) != 0;
}

static uint32_t scheduler_next_ready(uint32_t current_pid)
{
    uint32_t cpu_id = process_current_cpu_id();
    cpu_local_t* local = cpu_local_current();
    uint32_t idle_pid = local && local->self == local &&
        local->online != 0U ?
        __atomic_load_n(&local->idle_pid, __ATOMIC_ACQUIRE) : 1U;

    for (uint32_t offset = 1; offset < PROCESS_MAX; offset++)
    {
        uint32_t pid = (current_pid + offset) % PROCESS_MAX;
        if (pid == idle_pid)
            continue;

        process_t process;
        if (process_snapshot(pid, &process) &&
            process.state == PROCESS_READY &&
            process.owner_cpu == PROCESS_CPU_UNOWNED &&
            (process.affinity_cpu == PROCESS_CPU_ANY ||
             process.affinity_cpu == cpu_id))
            return pid;
    }

    if (idle_pid == 0U || idle_pid >= PROCESS_MAX)
        return current_pid;

    process_t idle;
    if (process_snapshot(idle_pid, &idle) &&
        ((idle.state == PROCESS_READY &&
          idle.owner_cpu == PROCESS_CPU_UNOWNED &&
          (idle.affinity_cpu == PROCESS_CPU_ANY ||
           idle.affinity_cpu == cpu_id)) ||
         (idle.state == PROCESS_RUNNING && idle.owner_cpu == cpu_id)))
        return idle_pid;

    return current_pid;
}

static uint64_t scheduler_switch_from_stack(uint64_t current_stack,
                                           uint32_t current_pid)
{
    uint32_t next_pid = scheduler_next_ready(current_pid);
    if (next_pid == current_pid)
        return current_stack;

    process_t current, next;
    uint32_t cpu_id = process_current_cpu_id();
    if (cpu_id >= CPU_LOCAL_MAX ||
        __atomic_load_n(&pending_outgoing_pid[cpu_id], __ATOMIC_ACQUIRE) != 0U ||
        !process_snapshot(current_pid, &current) ||
        !process_snapshot(next_pid, &next) ||
        !process_stack_is_valid(next_pid))
        return current_stack;

    uint64_t next_stack = process_get_stack(next_pid);
    if (next_stack == 0U ||
        !process_prepare_context_switch(next_pid, current_pid, cpu_id))
        return current_stack;

    /*
     * The outgoing process is deliberately still owned by this CPU. The
     * interrupt stub calls scheduler_finish_context_switch only after it has
     * changed RSP to next_stack, so migration and PID/stack reuse cannot race
     * the final instructions on the outgoing stack.
     */
    __atomic_store_n(&pending_outgoing_pid[cpu_id], current_pid,
                     __ATOMIC_RELEASE);

    if (next.address_space != paging_get_current_address_space())
        paging_switch_address_space(next.address_space);

    return next_stack;
}

/*
 * Called by the IRQ/IPI return stubs after RSP has changed to the incoming
 * saved frame. The outgoing stack is now inactive and can safely be released.
 */
void scheduler_finish_context_switch(void)
{
    /*
     * CPU 0 is also the legacy single-CPU fallback when GS/APIC setup is
     * unavailable, so use the process layer's canonical CPU ID rather than
     * requiring a cpu_local_t here.
     */
    uint32_t cpu_id = process_current_cpu_id();
    if (cpu_id >= CPU_LOCAL_MAX)
        kernel_panic_halt_all();

    uint32_t outgoing_pid = __atomic_exchange_n(
        &pending_outgoing_pid[cpu_id], 0U, __ATOMIC_ACQ_REL);
    if (outgoing_pid == 0U)
        return;

    if (!process_finish_context_switch(outgoing_pid, cpu_id))
        kernel_panic_halt_all();
}


uint64_t scheduler_start_cpu(uint64_t interrupted_stack)
{
    if (__atomic_load_n(&initialized, __ATOMIC_ACQUIRE) == 0 ||
        !process_is_initialized())
        return interrupted_stack;

    cpu_local_t* local = cpu_local_current();
    if (!local || local->self != local || local->online == 0U ||
        local->current_pid != 0U ||
        __atomic_load_n(&local->scheduler_online, __ATOMIC_ACQUIRE) == 0U)
        return interrupted_stack;

    uint32_t idle_pid = __atomic_load_n(&local->idle_pid, __ATOMIC_ACQUIRE);
    process_t idle;
    if (idle_pid == 0U || idle_pid >= PROCESS_MAX ||
        !process_snapshot(idle_pid, &idle) ||
        idle.state != PROCESS_READY ||
        idle.affinity_cpu != local->logical_id ||
        !process_stack_is_valid(idle_pid) ||
        !process_claim_for_cpu(idle_pid, 0U, local->logical_id))
    {
        __atomic_store_n(&local->reschedule_pending, 1U, __ATOMIC_RELEASE);
        return interrupted_stack;
    }

    __atomic_store_n(&local->reschedule_pending, 0U, __ATOMIC_RELEASE);
    if (idle.address_space != paging_get_current_address_space())
        paging_switch_address_space(idle.address_space);
    return process_get_stack(idle_pid);
}

uint64_t scheduler_reschedule(uint64_t current_stack)
{
    if (__atomic_load_n(&initialized, __ATOMIC_ACQUIRE) == 0 ||
        !process_is_initialized())
        return current_stack;

    uint32_t current_pid = process_current_pid();
    if (current_pid == 0 || current_pid >= PROCESS_MAX)
        return current_stack;

    process_save_stack(current_pid, current_stack);
    *scheduler_quantum_slot() = 0;
    return scheduler_switch_from_stack(current_stack, current_pid);
}

uint64_t scheduler_tick(uint64_t current_stack)
{
    if (__atomic_load_n(&initialized, __ATOMIC_ACQUIRE) == 0 ||
        !process_is_initialized())
        return current_stack;

    uint32_t current_pid = process_current_pid();
    if (current_pid == 0 || current_pid >= PROCESS_MAX)
        return current_stack;

    process_save_stack(current_pid, current_stack);
    cpu_local_t* local = cpu_local_current();
    int requested = local && local->self == local &&
        __atomic_exchange_n(&local->reschedule_pending, 0U,
                            __ATOMIC_ACQ_REL) != 0U;

    volatile uint32_t* quantum_ticks = scheduler_quantum_slot();
    if (!requested && ++(*quantum_ticks) < SCHEDULER_QUANTUM_TICKS)
        return current_stack;
    *quantum_ticks = 0;

    return scheduler_switch_from_stack(current_stack, current_pid);
}

void scheduler_yield(void)
{
    uint32_t pid = process_current_pid();
    if (pid != 0)
        process_mark_ready(pid);
}
