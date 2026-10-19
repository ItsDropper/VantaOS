/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "process_internal.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

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

    process_t* next = &process_table_records[next_pid];
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
        process_t* current = &process_table_records[current_pid];
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
    {
        serial_write("VANTAOS_SWITCH_REJECT_ARGS\\n");
        return 0;
    }

    uint64_t flags = process_table_lock_acquire();
    if (cpu_id != process_current_cpu_id() ||
        current_pid != process_current_slot() || next_pid == current_pid)
    {
        serial_write("VANTAOS_SWITCH_REJECT_CURRENT\\n");
        process_table_lock_release(flags);
        return 0;
    }

    process_t* current = &process_table_records[current_pid];
    process_t* next = &process_table_records[next_pid];
    if (current->owner_cpu != cpu_id ||
        (current->state != PROCESS_RUNNING &&
         current->state != PROCESS_READY &&
         current->state != PROCESS_SLEEPING &&
         current->state != PROCESS_TERMINATED))
    {
        serial_write("VANTAOS_SWITCH_REJECT_OUTGOING\\n");
        process_table_lock_release(flags);
        return 0;
    }
    if (next->state != PROCESS_READY ||
        next->owner_cpu != PROCESS_CPU_UNOWNED ||
        (next->affinity_cpu != PROCESS_CPU_ANY &&
         next->affinity_cpu != cpu_id))
    {
        serial_write("VANTAOS_SWITCH_REJECT_INCOMING_STATE\\n");
        process_table_lock_release(flags);
        return 0;
    }
    if (!process_stack_is_valid_locked(next) || next->address_space == NULL)
    {
        serial_write("VANTAOS_SWITCH_REJECT_INCOMING_CONTEXT\\n");
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
    if (!process_initialized_state || outgoing_pid == 0U ||
        outgoing_pid >= PROCESS_MAX ||
        cpu_id != process_current_cpu_id() ||
        outgoing_pid == process_current_slot())
        return 0;

    process_t outgoing_snapshot;
    uint64_t flags = process_table_lock_acquire();
    process_t* outgoing = &process_table_records[outgoing_pid];
    if (outgoing->owner_cpu != cpu_id ||
        (outgoing->state != PROCESS_READY &&
         outgoing->state != PROCESS_SLEEPING &&
         outgoing->state != PROCESS_TERMINATED))
    {
        process_table_lock_release(flags);
        return 0;
    }

    if (outgoing->state != PROCESS_TERMINATED)
    {
        outgoing->owner_cpu = PROCESS_CPU_UNOWNED;
        process_table_lock_release(flags);
        return 1;
    }

    /*
     * Keep ownership pinned while releasing the lock. Other CPUs cannot reap
     * or reuse this PID, and the interrupt return path has already switched
     * away from both its kernel stack and address space.
     */
    outgoing_snapshot = *outgoing;
    int outgoing_was_user = outgoing_snapshot.is_user != 0U;
    process_table_lock_release(flags);

    if (!process_release_user_resources(&outgoing_snapshot))
        return 0;

    uint32_t wake_parent_pid = 0U;
    flags = process_table_lock_acquire();
    outgoing = &process_table_records[outgoing_pid];
    if (outgoing->owner_cpu != cpu_id ||
        outgoing->state != PROCESS_TERMINATED)
    {
        process_table_lock_release(flags);
        return 0;
    }

    uint32_t parent_pid = outgoing->parent_pid;
    int retain_zombie = 0;
    if (outgoing_was_user && parent_pid > 0U &&
        parent_pid < PROCESS_MAX)
    {
        const process_t* parent = &process_table_records[parent_pid];
        retain_zombie = parent->is_user &&
            parent->state != PROCESS_UNUSED &&
            parent->state != PROCESS_TERMINATED &&
            parent->state != PROCESS_ZOMBIE &&
            parent->state != PROCESS_REAPING;
    }

    /* A process that exits cannot remain the parent of live descendants. */
    process_reparent_children_locked(outgoing_pid);

    if (retain_zombie)
    {
        int32_t exit_status = outgoing->exit_status;
        char name[PROCESS_NAME_MAX + 1U];
        for (uint32_t i = 0; i < sizeof(name); i++)
            name[i] = outgoing->name[i];

        process_reset_record(outgoing);
        outgoing->pid = outgoing_pid;
        outgoing->parent_pid = parent_pid;
        outgoing->exit_status = exit_status;
        for (uint32_t i = 0; i < sizeof(name); i++)
            outgoing->name[i] = name[i];
        outgoing->state = PROCESS_ZOMBIE;
        outgoing->owner_cpu = PROCESS_CPU_UNOWNED;
        outgoing->affinity_cpu = PROCESS_CPU_ANY;
        wake_parent_pid = parent_pid;
    }
    else
    {
        process_reset_record(outgoing);
        outgoing->pid = outgoing_pid;
    }
    process_table_lock_release(flags);

    if (wake_parent_pid != 0U)
    {
        serial_write("VANTAOS_WAITPID_CHILD_ZOMBIE\n");
        if (process_wake(wake_parent_pid))
            serial_write("VANTAOS_WAITPID_PARENT_WOKEN\n");
        else
            serial_write("VANTAOS_WAITPID_PARENT_NOT_SLEEPING\n");
    }
    return 1;
}

int process_switch_to(uint32_t pid)
{
    return process_claim_for_cpu(pid, process_current_slot(),
                                 process_current_cpu_id());
}
