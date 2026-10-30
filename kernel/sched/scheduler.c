/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <stddef.h>
#include "scheduler.h"
#include "ktrace.h"
#include "cpu_local.h"
#include "process.h"
#include "paging.h"
#include "panic.h"
#include "gdt.h"
#include "serial.h"

extern unsigned char stack_bottom;
extern unsigned char stack_top;

#define SCHEDULER_QUANTUM_TICKS 5U

static int initialized;
static uint32_t fallback_quantum_ticks;
static uint32_t fallback_scheduler_cursor = 1U;
/* PID being switched away from; consumed after interrupt assembly changes RSP. */
static volatile uint32_t pending_outgoing_pid[CPU_LOCAL_MAX];

static volatile uint32_t* scheduler_quantum_slot(void)
{
    cpu_local_t* local = cpu_local_current();
    if (local && local->self == local && local->online != 0U)
        return &local->scheduler_ticks;
    return &fallback_quantum_ticks;
}

static volatile uint32_t* scheduler_cursor_slot(void)
{
    cpu_local_t* local = cpu_local_current();
    if (local && local->self == local && local->online != 0U)
        return &local->scheduler_cursor;
    return &fallback_scheduler_cursor;
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

/*
 * Select a candidate without claiming it. Another CPU may win the race
 * between this snapshot and process_prepare_context_switch(); the caller
 * passes that rejected PID back on the next scan so one lost race cannot
 * waste the entire scheduling quantum.
 */
static uint32_t scheduler_next_ready(uint32_t current_pid,
                                     uint32_t rejected_pid)
{
    uint32_t cpu_id = process_current_cpu_id();
    cpu_local_t* local = cpu_local_current();
    uint32_t idle_pid = local && local->self == local &&
        local->online != 0U ?
        __atomic_load_n(&local->idle_pid, __ATOMIC_ACQUIRE) : 1U;

    /*
     * Each CPU advances its own scan origin after dispatch. The process layer
     * scans under one table-lock acquisition; the subsequent context-switch
     * claim remains the race-safe ownership point.
     */
    uint32_t cursor = __atomic_load_n(scheduler_cursor_slot(),
                                     __ATOMIC_RELAXED);
    return process_find_next_ready(cursor, current_pid, rejected_pid,
                                   cpu_id, idle_pid);
}

static int scheduler_install_kernel_stack(uint32_t pid)
{
    process_t process;
    if (!process_snapshot(pid, &process) || !process.kernel_stack)
        return 0;

    uintptr_t top;
    if (process.kernel_stack == (void*)(uintptr_t)&stack_bottom)
        top = (uintptr_t)&stack_top;
    else
        top = (uintptr_t)process.kernel_stack + PROCESS_STACK_SIZE;

    return gdt_set_kernel_stack(process_current_cpu_id(), top);
}

static uint64_t scheduler_switch_from_stack(uint64_t current_stack,
                                           uint32_t current_pid)
{
    uint32_t cpu_id = process_current_cpu_id();
    cpu_local_t* local = cpu_local_current();
    uint64_t next_stack = 0;
    paging_address_space_t* next_address_space = NULL;
    uint32_t next_pid = current_pid;
    uint32_t candidates[PROCESS_MAX];
    int prepared = 0;

    if (cpu_id >= CPU_LOCAL_MAX)
        return current_stack;
    if (__atomic_load_n(&pending_outgoing_pid[cpu_id], __ATOMIC_ACQUIRE) != 0U)
    {
        if (current_pid != 0U)
            serial_write("VANTAOS_SCHED_PENDING_OUTGOING\\n");
        return current_stack;
    }

    /*
     * Collect a complete ordered candidate snapshot once. Each claim remains
     * serialized by process_prepare_context_switch(), but CPUs no longer
     * contend on the global process lock for a full rescan after every lost
     * candidate race.
     */
    uint32_t idle_pid = local && local->self == local &&
        local->online != 0U ?
        __atomic_load_n(&local->idle_pid, __ATOMIC_ACQUIRE) : 1U;
    uint32_t cursor = __atomic_load_n(scheduler_cursor_slot(),
                                      __ATOMIC_RELAXED);
    size_t candidate_count = process_collect_ready(
        cursor, current_pid, cpu_id, idle_pid, candidates, PROCESS_MAX);

    for (size_t i = 0U; i < candidate_count; i++)
    {
        next_pid = candidates[i];
        if (process_prepare_context_switch(next_pid, current_pid, cpu_id,
                                           &next_stack, &next_address_space))
            {
                uint32_t next_cursor = (next_pid + 1U) % PROCESS_MAX;
                if (next_cursor == 0U)
                    next_cursor = 1U;
                __atomic_store_n(scheduler_cursor_slot(), next_cursor,
                                 __ATOMIC_RELAXED);
                prepared = 1;
                break;
            }
    }

    if (!prepared)
    {
        process_t current;
        if (process_snapshot(current_pid, &current) &&
            current.state != PROCESS_RUNNING && current.state != PROCESS_READY)
            serial_write("VANTAOS_SCHED_NO_RUNNABLE\\n");
        return current_stack;
    }

    /*
     * The outgoing process is deliberately still owned by this CPU. The
     * interrupt stub calls scheduler_finish_context_switch only after it has
     * changed RSP to next_stack, so migration and PID/stack reuse cannot race
     * the final instructions on the outgoing stack.
     */
    __atomic_store_n(&pending_outgoing_pid[cpu_id], current_pid,
                     __ATOMIC_RELEASE);

    if (next_address_space != paging_get_current_address_space())
        paging_switch_address_space(next_address_space);

    if (!scheduler_install_kernel_stack(next_pid))
        kernel_panic_halt_all();

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

    ktrace_emit(KTRACE_EVENT_SCHED_SWITCH, cpu_id, outgoing_pid,
                process_current_pid());
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
    uint64_t idle_stack = 0;
    paging_address_space_t* idle_address_space = NULL;
    if (idle_pid == 0U || idle_pid >= PROCESS_MAX ||
        !process_claim_for_cpu_context(idle_pid, 0U, local->logical_id,
                                       &idle_stack, &idle_address_space))
    {
        __atomic_store_n(&local->reschedule_pending, 1U, __ATOMIC_RELEASE);
        return interrupted_stack;
    }

    __atomic_store_n(&local->reschedule_pending, 0U, __ATOMIC_RELEASE);
    if (idle_address_space != paging_get_current_address_space())
        paging_switch_address_space(idle_address_space);

    if (!scheduler_install_kernel_stack(idle_pid))
        kernel_panic_halt_all();
    return idle_stack;
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

    /*
     * Sleeping and terminating processes must never continue executing merely
     * because their round-robin quantum has not expired. In particular,
     * waitpid marks its caller SLEEPING from inside the syscall path; the very
     * next timer interrupt must switch to the runnable child (or idle task).
     */
    process_t current;
    int current_runnable = process_snapshot(current_pid, &current) &&
        (current.state == PROCESS_RUNNING || current.state == PROCESS_READY);
    if (!current_runnable)
        requested = 1;

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

void scheduler_request_reschedule(void)
{
    cpu_local_t* local = cpu_local_current();
    if (local && local->self == local && local->online != 0U)
    {
        __atomic_store_n(&local->reschedule_pending, 1U, __ATOMIC_RELEASE);
        return;
    }

    /* Legacy single-CPU path has no per-CPU pending flag. */
    __atomic_store_n(&fallback_quantum_ticks,
                     SCHEDULER_QUANTUM_TICKS - 1U, __ATOMIC_RELEASE);
}
