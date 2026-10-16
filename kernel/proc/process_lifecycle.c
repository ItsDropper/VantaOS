#include "process_internal.h"
#include "apic.h"

#include <stddef.h>
#include <stdint.h>

int process_mark_ready(uint32_t pid)
{
    if (!process_initialized_state || pid == 0 || pid >= PROCESS_MAX)
        return 0;
    uint64_t flags = process_table_lock_acquire();
    int ok = process_table_records[pid].state != PROCESS_UNUSED &&
             process_table_records[pid].state != PROCESS_TERMINATED &&
             process_table_records[pid].state != PROCESS_REAPING;
    if (ok)
        process_table_records[pid].state = PROCESS_READY;
    process_table_lock_release(flags);
    return ok;
}

int process_set_running(uint32_t pid)
{
    if (!process_initialized_state || pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    process_t* target = &process_table_records[pid];
    uint32_t cpu_id = process_current_cpu_id();
    if (target->state == PROCESS_UNUSED ||
        target->state == PROCESS_TERMINATED ||
        target->state == PROCESS_REAPING ||
        (target->affinity_cpu != PROCESS_CPU_ANY &&
         target->affinity_cpu != cpu_id) ||
        (target->owner_cpu != PROCESS_CPU_UNOWNED &&
         target->owner_cpu != cpu_id))
    {
        process_table_lock_release(flags);
        return 0;
    }

    uint32_t current_pid = process_current_slot();
    if (current_pid < PROCESS_MAX && current_pid != pid)
    {
        process_t* current = &process_table_records[current_pid];
        if (current->state == PROCESS_RUNNING)
            current->state = PROCESS_READY;
        if (current->owner_cpu == cpu_id)
            current->owner_cpu = PROCESS_CPU_UNOWNED;
    }

    process_set_current_slot(pid);
    target->state = PROCESS_RUNNING;
    target->owner_cpu = cpu_id;
    process_table_lock_release(flags);
    return 1;
}

static void process_send_wake_ipi(uint32_t owner_cpu,
                                       uint32_t affinity_cpu)
{
    if (!apic_is_enabled())
        return;

    if (owner_cpu != PROCESS_CPU_UNOWNED)
    {
        const cpu_local_t* local = cpu_local_get(owner_cpu);
        if (local)
            (void)apic_send_reschedule_ipi(local->apic_id);
        return;
    }

    if (affinity_cpu != PROCESS_CPU_ANY)
    {
        const cpu_local_t* local = cpu_local_get(affinity_cpu);
        if (local)
            (void)apic_send_reschedule_ipi(local->apic_id);
        return;
    }

    /* An unpinned task can be claimed by any online CPU. */
    for (uint32_t cpu = 0; cpu < cpu_local_capacity(); cpu++)
    {
        const cpu_local_t* local = cpu_local_get(cpu);
        if (local)
            (void)apic_send_reschedule_ipi(local->apic_id);
    }
}

int process_wake(uint32_t pid)
{
    if (!process_initialized_state || pid == 0U || pid >= PROCESS_MAX)
        return 0;

    uint32_t owner_cpu = PROCESS_CPU_UNOWNED;
    uint32_t affinity_cpu = PROCESS_CPU_ANY;
    uint64_t flags = process_table_lock_acquire();
    process_t* process = &process_table_records[pid];
    int ok = process->state == PROCESS_SLEEPING;
    if (ok)
    {
        process->state = PROCESS_READY;
        owner_cpu = process->owner_cpu;
        affinity_cpu = process->affinity_cpu;
    }
    process_table_lock_release(flags);

    if (ok)
        process_send_wake_ipi(owner_cpu, affinity_cpu);
    return ok;
}


static void process_request_cpu_reschedule(uint32_t cpu_id)
{
    const cpu_local_t* local = cpu_local_get(cpu_id);
    if (local && apic_is_enabled())
        (void)apic_send_reschedule_ipi(local->apic_id);
}

int process_set_affinity(uint32_t pid, uint32_t cpu_id)
{
    if (!process_initialized_state || pid == 0U || pid >= PROCESS_MAX ||
        (cpu_id != PROCESS_CPU_ANY &&
         (cpu_id >= CPU_LOCAL_MAX || !cpu_local_get(cpu_id))))
        return 0;

    uint32_t old_affinity = PROCESS_CPU_ANY;
    uint32_t owner_cpu = PROCESS_CPU_UNOWNED;
    uint64_t flags = process_table_lock_acquire();
    process_t* process = &process_table_records[pid];
    if (process->state == PROCESS_UNUSED ||
        process->state == PROCESS_TERMINATED ||
        process->state == PROCESS_REAPING)
    {
        process_table_lock_release(flags);
        return 0;
    }

    old_affinity = process->affinity_cpu;
    owner_cpu = process->owner_cpu;
    if (old_affinity == cpu_id)
    {
        process_table_lock_release(flags);
        return 1;
    }

    process->affinity_cpu = cpu_id;
    process_table_lock_release(flags);

    /*
     * First make the old owner relinquish its stack, then prompt the
     * destination (or all CPUs for an unpinned process) to claim READY work.
     */
    if (owner_cpu != PROCESS_CPU_UNOWNED)
        process_request_cpu_reschedule(owner_cpu);

    if (cpu_id != PROCESS_CPU_ANY)
    {
        if (cpu_id != owner_cpu)
            process_request_cpu_reschedule(cpu_id);
    }
    else
    {
        for (uint32_t cpu = 0; cpu < cpu_local_capacity(); cpu++)
        {
            if (cpu != owner_cpu)
                process_request_cpu_reschedule(cpu);
        }
    }
    return 1;
}

void process_block_current(void)
{
    if (!process_initialized_state)
        return;
    uint64_t flags = process_table_lock_acquire();
    uint32_t current_pid = process_current_slot();
    if (current_pid < PROCESS_MAX)
        process_table_records[current_pid].state = PROCESS_SLEEPING;
    /* Ownership remains with this CPU until it actually switches stacks. */
    process_table_lock_release(flags);
}

int process_terminate(uint32_t pid)
{
    if (!process_initialized_state || pid == 0 || pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    process_t* process = &process_table_records[pid];
    if (process->state == PROCESS_UNUSED ||
        process->state == PROCESS_TERMINATED ||
        process->state == PROCESS_REAPING ||
        process_is_idle_locked(pid))
    {
        process_table_lock_release(flags);
        return 0;
    }

    process->state = PROCESS_TERMINATED;
    int is_current = pid == process_current_slot();
    process_table_lock_release(flags);

    if (is_current)
        return 1;
    return process_reap(pid);
}

void process_terminate_current(void)
{
    if (!process_initialized_state)
        return;
    uint64_t flags = process_table_lock_acquire();
    uint32_t current_pid = process_current_slot();
    if (current_pid != 0 && current_pid < PROCESS_MAX &&
        process_table_records[current_pid].state != PROCESS_UNUSED &&
        !process_is_idle_locked(current_pid))
        process_table_records[current_pid].state = PROCESS_TERMINATED;
    process_table_lock_release(flags);
}

void process_save_stack(uint32_t pid, uint64_t stack_pointer)
{
    if (!process_initialized_state || pid >= PROCESS_MAX)
        return;
    uint64_t flags = process_table_lock_acquire();
    if (process_table_records[pid].state != PROCESS_UNUSED)
    {
        process_table_records[pid].stack_pointer = stack_pointer;
        process_table_records[pid].context.rsp = stack_pointer;
    }
    process_table_lock_release(flags);
}

uint64_t process_get_stack(uint32_t pid)
{
    if (!process_initialized_state || pid >= PROCESS_MAX)
        return 0;
    uint64_t flags = process_table_lock_acquire();
    uint64_t stack = process_table_records[pid].state == PROCESS_UNUSED ?
                     0 : process_table_records[pid].stack_pointer;
    process_table_lock_release(flags);
    return stack;
}

int process_stack_is_valid(uint32_t pid)
{
    if (!process_initialized_state || pid == 0 || pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    int valid = process_stack_is_valid_locked(&process_table_records[pid]);
    process_table_lock_release(flags);
    return valid;
}

int process_reap(uint32_t pid)
{
    if (!process_initialized_state || pid == 0U || pid >= PROCESS_MAX)
        return 0;

    process_t snapshot;
    uint64_t flags = process_table_lock_acquire();
    process_t* process = &process_table_records[pid];
    if (pid == process_current_slot() ||
        process_is_idle_locked(pid) ||
        process->state != PROCESS_TERMINATED ||
        process->owner_cpu != PROCESS_CPU_UNOWNED)
    {
        process_table_lock_release(flags);
        return 0;
    }

    snapshot = *process;
    process->state = PROCESS_REAPING;
    process_table_lock_release(flags);

    if (!process_release_user_resources(&snapshot))
    {
        flags = process_table_lock_acquire();
        process = &process_table_records[pid];
        if (process->state == PROCESS_REAPING)
            process->state = PROCESS_TERMINATED;
        process_table_lock_release(flags);
        return 0;
    }

    flags = process_table_lock_acquire();
    process = &process_table_records[pid];
    if (process->state != PROCESS_REAPING ||
        process->owner_cpu != PROCESS_CPU_UNOWNED)
    {
        process_table_lock_release(flags);
        return 0;
    }
    process_reset_record(process);
    process->pid = pid;
    process_table_lock_release(flags);
    return 1;
}

uint64_t process_schedule(uint64_t current_stack)
{
    uint32_t current_pid = process_current_slot();
    if (!process_initialized_state || current_pid >= PROCESS_MAX)
        return current_stack;

    process_save_stack(current_pid, current_stack);
    return current_stack;
}


int process_snapshot(uint32_t pid, process_t* snapshot)
{
    if (!process_initialized_state || pid >= PROCESS_MAX || !snapshot)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    int ok = process_table_records[pid].state != PROCESS_UNUSED;
    if (ok)
        *snapshot = process_table_records[pid];
    process_table_lock_release(flags);
    return ok;
}


int process_snapshot_try(uint32_t pid, process_t* snapshot)
{
    if (!process_initialized_state || pid >= PROCESS_MAX || !snapshot)
        return 0;

    uint64_t flags;
    if (!spinlock_try_lock_irqsave(&process_table_lock, &flags))
        return 0;

    int ok = process_table_records[pid].state != PROCESS_UNUSED;
    if (ok)
        *snapshot = process_table_records[pid];
    process_table_lock_release(flags);
    return ok;
}

unsigned int process_count(void)
{
    if (!process_initialized_state)
        return 0;
    uint64_t flags = process_table_lock_acquire();
    unsigned int count = 0;
    for (uint32_t i = 0; i < PROCESS_MAX; i++)
        if (process_table_records[i].state != PROCESS_UNUSED &&
            process_table_records[i].state != PROCESS_TERMINATED &&
            process_table_records[i].state != PROCESS_REAPING)
            count++;
    process_table_lock_release(flags);
    return count;
}

uint32_t process_current_pid(void)
{
    return process_current_slot();
}
