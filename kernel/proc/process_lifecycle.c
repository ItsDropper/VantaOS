#include "process_internal.h"
#include "apic.h"

#include <stddef.h>
#include <stdint.h>

int process_mark_ready(uint32_t pid)
{
    if (!process_initialized_state || pid == 0 || pid >= PROCESS_MAX)
        return 0;
    uint64_t flags = process_table_lock_acquire();
    int ok = processes[pid].state != PROCESS_UNUSED &&
             processes[pid].state != PROCESS_TERMINATED;
    if (ok)
        processes[pid].state = PROCESS_READY;
    process_table_lock_release(flags);
    return ok;
}

int process_set_running(uint32_t pid)
{
    if (!process_initialized_state || pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    process_t* target = &processes[pid];
    uint32_t cpu_id = process_current_cpu_id();
    if (target->state == PROCESS_UNUSED ||
        target->state == PROCESS_TERMINATED ||
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
        process_t* current = &processes[current_pid];
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

static int process_claim_for_cpu_internal(uint32_t next_pid,
                                          uint32_t current_pid,
                                          uint32_t cpu_id,
                                          uint64_t* stack_pointer_out,
                                          paging_address_space_t** address_space_out)
{
    if (!process_initialized_state || next_pid == 0 || next_pid >= PROCESS_MAX ||
        current_pid >= PROCESS_MAX ||
        ((stack_pointer_out == NULL) != (address_space_out == NULL)))
        return 0;

    uint64_t flags = process_table_lock_acquire();
    if (cpu_id != process_current_cpu_id() ||
        current_pid != process_current_slot())
    {
        process_table_lock_release(flags);
        return 0;
    }

    process_t* next = &processes[next_pid];
    if (next->state != PROCESS_READY ||
        next->owner_cpu != PROCESS_CPU_UNOWNED ||
        (next->affinity_cpu != PROCESS_CPU_ANY &&
         next->affinity_cpu != cpu_id) ||
        (stack_pointer_out &&
         (!process_stack_is_valid_locked(next) ||
          next->address_space == NULL)))
    {
        process_table_lock_release(flags);
        return 0;
    }

    if (current_pid != 0 && current_pid != next_pid)
    {
        process_t* current = &processes[current_pid];
        if (current->owner_cpu != cpu_id ||
            (current->state != PROCESS_RUNNING &&
             current->state != PROCESS_READY &&
             current->state != PROCESS_SLEEPING &&
             current->state != PROCESS_TERMINATED))
        {
            process_table_lock_release(flags);
            return 0;
        }

        if (current->state == PROCESS_RUNNING)
            current->state = PROCESS_READY;
        current->owner_cpu = PROCESS_CPU_UNOWNED;
    }

    next->state = PROCESS_RUNNING;
    next->owner_cpu = cpu_id;
    process_set_current_slot(next_pid);
    if (stack_pointer_out)
    {
        *stack_pointer_out = next->stack_pointer;
        *address_space_out = next->address_space;
    }
    process_table_lock_release(flags);
    return 1;
}

int process_claim_for_cpu(uint32_t next_pid, uint32_t current_pid,
                          uint32_t cpu_id)
{
    return process_claim_for_cpu_internal(next_pid, current_pid, cpu_id,
                                          NULL, NULL);
}

/* Claim a READY process and return its validated context under the same lock. */
int process_claim_for_cpu_context(uint32_t next_pid, uint32_t current_pid,
                                  uint32_t cpu_id, uint64_t* stack_pointer_out,
                                  paging_address_space_t** address_space_out)
{
    if (!stack_pointer_out || !address_space_out)
        return 0;
    return process_claim_for_cpu_internal(next_pid, current_pid, cpu_id,
                                          stack_pointer_out, address_space_out);
}

/*
 * Scheduler-only transition. The outgoing process remains owned by this CPU
 * until the interrupt-return assembly has switched RSP to the incoming stack.
 * That prevents another CPU from resuming a stack that is still active here.
 */
int process_prepare_context_switch(uint32_t next_pid, uint32_t current_pid,
                                   uint32_t cpu_id, uint64_t* next_stack_out,
                                   paging_address_space_t** next_address_space_out)
{
    if (!process_initialized_state || next_pid == 0U || next_pid >= PROCESS_MAX ||
        current_pid == 0U || current_pid >= PROCESS_MAX ||
        !next_stack_out || !next_address_space_out)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    if (cpu_id != process_current_cpu_id() ||
        current_pid != process_current_slot() || next_pid == current_pid)
    {
        process_table_lock_release(flags);
        return 0;
    }

    process_t* current = &processes[current_pid];
    process_t* next = &processes[next_pid];
    if (current->owner_cpu != cpu_id ||
        (current->state != PROCESS_RUNNING &&
         current->state != PROCESS_READY &&
         current->state != PROCESS_SLEEPING &&
         current->state != PROCESS_TERMINATED) ||
        next->state != PROCESS_READY ||
        next->owner_cpu != PROCESS_CPU_UNOWNED ||
        (next->affinity_cpu != PROCESS_CPU_ANY &&
         next->affinity_cpu != cpu_id) ||
        !process_stack_is_valid_locked(next) ||
        next->address_space == NULL)
    {
        process_table_lock_release(flags);
        return 0;
    }

    if (current->state == PROCESS_RUNNING)
        current->state = PROCESS_READY;

    /* Keep current->owner_cpu until process_finish_context_switch(). */
    next->state = PROCESS_RUNNING;
    next->owner_cpu = cpu_id;
    process_set_current_slot(next_pid);
    *next_stack_out = next->stack_pointer;
    *next_address_space_out = next->address_space;
    process_table_lock_release(flags);
    return 1;
}

/*
 * Called from interrupt-return assembly only after RSP points at the incoming
 * process's saved frame. Releasing a terminated slot here is safe: its stack
 * is no longer active, and slot reuse is atomic with the ownership release.
 */
int process_finish_context_switch(uint32_t outgoing_pid, uint32_t cpu_id)
{
    if (!process_initialized_state || outgoing_pid == 0U || outgoing_pid >= PROCESS_MAX ||
        cpu_id != process_current_cpu_id() ||
        outgoing_pid == process_current_slot())
        return 0;

    uint64_t flags = process_table_lock_acquire();
    process_t* outgoing = &processes[outgoing_pid];
    if (outgoing->owner_cpu != cpu_id ||
        (outgoing->state != PROCESS_READY &&
         outgoing->state != PROCESS_SLEEPING &&
         outgoing->state != PROCESS_TERMINATED))
    {
        process_table_lock_release(flags);
        return 0;
    }

    if (outgoing->state == PROCESS_TERMINATED)
    {
        process_reset_record(outgoing);
        outgoing->pid = outgoing_pid;
    }
    else
    {
        outgoing->owner_cpu = PROCESS_CPU_UNOWNED;
    }

    process_table_lock_release(flags);
    return 1;
}

int process_switch_to(uint32_t pid)
{
    return process_claim_for_cpu(pid, process_current_slot(),
                                 process_current_cpu_id());
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
    process_t* process = &processes[pid];
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
    process_t* process = &processes[pid];
    if (process->state == PROCESS_UNUSED ||
        process->state == PROCESS_TERMINATED)
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
        processes[current_pid].state = PROCESS_SLEEPING;
    /* Ownership remains with this CPU until it actually switches stacks. */
    process_table_lock_release(flags);
}

int process_terminate(uint32_t pid)
{
    if (!process_initialized_state || pid == 0 || pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    process_t* process = &processes[pid];
    if (process->state == PROCESS_UNUSED ||
        process->state == PROCESS_TERMINATED ||
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
        processes[current_pid].state != PROCESS_UNUSED &&
        !process_is_idle_locked(current_pid))
        processes[current_pid].state = PROCESS_TERMINATED;
    process_table_lock_release(flags);
}

void process_save_stack(uint32_t pid, uint64_t stack_pointer)
{
    if (!process_initialized_state || pid >= PROCESS_MAX)
        return;
    uint64_t flags = process_table_lock_acquire();
    if (processes[pid].state != PROCESS_UNUSED)
    {
        processes[pid].stack_pointer = stack_pointer;
        processes[pid].context.rsp = stack_pointer;
    }
    process_table_lock_release(flags);
}

uint64_t process_get_stack(uint32_t pid)
{
    if (!process_initialized_state || pid >= PROCESS_MAX)
        return 0;
    uint64_t flags = process_table_lock_acquire();
    uint64_t stack = processes[pid].state == PROCESS_UNUSED ?
                     0 : processes[pid].stack_pointer;
    process_table_lock_release(flags);
    return stack;
}

int process_stack_is_valid(uint32_t pid)
{
    if (!process_initialized_state || pid == 0 || pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    int valid = process_stack_is_valid_locked(&processes[pid]);
    process_table_lock_release(flags);
    return valid;
}

int process_reap(uint32_t pid)
{
    if (!process_initialized_state || pid == 0 || pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    if (pid == process_current_slot() ||
        process_is_idle_locked(pid) ||
        processes[pid].state != PROCESS_TERMINATED ||
        processes[pid].owner_cpu != PROCESS_CPU_UNOWNED)
    {
        process_table_lock_release(flags);
        return 0;
    }

    process_t* process = &processes[pid];
    process->address_space = NULL;
    process->kernel_stack = NULL;
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
    int ok = processes[pid].state != PROCESS_UNUSED;
    if (ok)
        *snapshot = processes[pid];
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

    int ok = processes[pid].state != PROCESS_UNUSED;
    if (ok)
        *snapshot = processes[pid];
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
        if (processes[i].state != PROCESS_UNUSED &&
            processes[i].state != PROCESS_TERMINATED)
            count++;
    process_table_lock_release(flags);
    return count;
}

uint32_t process_current_pid(void)
{
    return process_current_slot();
}
