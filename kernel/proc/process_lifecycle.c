/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "process_internal.h"
#include "apic.h"
#include "scheduler.h"
#include "../ipc.h"

#include <stddef.h>
#include <stdint.h>

void process_reparent_children_locked(uint32_t parent_pid)
{
    if (parent_pid == 0U || parent_pid >= PROCESS_MAX)
        return;

    for (uint32_t pid = 1U; pid < PROCESS_MAX; pid++)
    {
        if (pid == parent_pid)
            continue;

        process_t* child = &process_table_records[pid];
        if (child->state == PROCESS_UNUSED ||
            child->parent_pid != parent_pid)
            continue;

        if (child->state == PROCESS_ZOMBIE &&
            child->owner_cpu == PROCESS_CPU_UNOWNED)
        {
            process_reset_record(child);
            child->pid = pid;
        }
        else
        {
            /* Orphans are detached; their later exit will be auto-reaped. */
            child->parent_pid = 0U;
        }
    }
}

int process_mark_ready(uint32_t pid)
{
    if (!process_initialized_state || pid == 0 || pid >= PROCESS_MAX)
        return 0;
    uint64_t flags = process_table_lock_acquire();
    int ok = process_table_records[pid].state != PROCESS_UNUSED &&
             process_table_records[pid].state != PROCESS_TERMINATED &&
             process_table_records[pid].state != PROCESS_ZOMBIE &&
             process_table_records[pid].state != PROCESS_REAPING;
    if (ok)
    {
        process_table_records[pid].state = PROCESS_READY;
        process_table_records[pid].sleep_deadline_ticks = 0U;
        process_table_records[pid].wait_queue = NULL;
    }
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
        target->state == PROCESS_ZOMBIE ||
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
    target->sleep_deadline_ticks = 0U;
    target->wait_queue = NULL;
    target->state = PROCESS_RUNNING;
    target->owner_cpu = cpu_id;
    process_table_lock_release(flags);
    return 1;
}

static volatile uint32_t wakeup_cpu_cursor;

/*
 * Estimate runnable load from the authoritative process table, then choose
 * one online CPU rather than broadcasting an IPI to every AP. Owned tasks
 * count against their current CPU; unowned CPU-pinned READY tasks count
 * against their affinity target. Unpinned READY tasks remain unassigned.
 */
static uint32_t process_choose_wakeup_cpu(uint32_t excluded_cpu)
{
    uint32_t load[CPU_LOCAL_MAX] = {0};
    uint64_t flags = process_table_lock_acquire();
    for (uint32_t pid = 1U; pid < PROCESS_MAX; pid++)
    {
        const process_t* process = &process_table_records[pid];
        if (process->state != PROCESS_READY &&
            process->state != PROCESS_RUNNING)
            continue;

        uint32_t cpu = process->owner_cpu;
        if (cpu == PROCESS_CPU_UNOWNED)
            cpu = process->affinity_cpu;
        if (cpu < CPU_LOCAL_MAX && cpu != PROCESS_CPU_ANY &&
            load[cpu] != UINT32_MAX)
            load[cpu]++;
    }
    process_table_lock_release(flags);

    uint32_t start = __atomic_fetch_add(&wakeup_cpu_cursor, 1U,
                                        __ATOMIC_RELAXED) %
                     cpu_local_capacity();
    uint32_t selected = PROCESS_CPU_UNOWNED;
    uint32_t least_load = UINT32_MAX;
    for (uint32_t offset = 0; offset < cpu_local_capacity(); offset++)
    {
        uint32_t cpu = (start + offset) % cpu_local_capacity();
        if (cpu == excluded_cpu || !cpu_local_get(cpu))
            continue;
        if (load[cpu] < least_load)
        {
            selected = cpu;
            least_load = load[cpu];
        }
    }
    return selected;
}

static void process_request_cpu_reschedule(uint32_t cpu_id)
{
    const cpu_local_t* local = cpu_local_get(cpu_id);
    if (!local)
        return;

    __atomic_store_n((uint32_t*)&local->reschedule_pending, 1U,
                     __ATOMIC_RELEASE);
    if (apic_is_enabled())
        (void)apic_send_reschedule_ipi(local->apic_id);
}

static void process_send_wake_ipi(uint32_t owner_cpu,
                                  uint32_t affinity_cpu)
{
    /*
     * Publish the scheduling request before attempting hardware delivery.
     * The BSP timer consumes this flag too, so a deferred/lost IPI cannot
     * leave a newly READY process stranded behind an idle context.
     */
    if (owner_cpu != PROCESS_CPU_UNOWNED)
    {
        process_request_cpu_reschedule(owner_cpu);
        return;
    }

    if (affinity_cpu != PROCESS_CPU_ANY)
    {
        process_request_cpu_reschedule(affinity_cpu);
        return;
    }

    /*
     * Unpinned work is eligible on every CPU. Wake the least-loaded eligible
     * CPU and let the next scheduler decision claim the task atomically;
     * other CPUs continue their own timers and may steal it if this CPU is
     * busy or loses a claim race.
     */
    uint32_t target = process_choose_wakeup_cpu(PROCESS_CPU_UNOWNED);
    if (target != PROCESS_CPU_UNOWNED)
        process_request_cpu_reschedule(target);
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
        process->sleep_deadline_ticks = 0U;
        process->wait_queue = NULL;
        owner_cpu = process->owner_cpu;
        affinity_cpu = process->affinity_cpu;
    }
    process_table_lock_release(flags);

    if (ok)
        process_send_wake_ipi(owner_cpu, affinity_cpu);
    return ok;
}


/*
 * Timed sleep uses the process table as the authoritative wait state. The
 * deadline is published in the same critical section as PROCESS_SLEEPING, so
 * the timer cannot observe a half-registered sleeper.
 */
int process_sleep_current_until(uint64_t deadline_ticks)
{
    if (!process_initialized_state || deadline_ticks == 0U)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    uint32_t pid = process_current_slot();
    if (pid == 0U || pid >= PROCESS_MAX ||
        process_is_idle_locked(pid) ||
        process_table_records[pid].state != PROCESS_RUNNING)
    {
        process_table_lock_release(flags);
        return 0;
    }

    process_t* process = &process_table_records[pid];
    process->sleep_deadline_ticks = deadline_ticks;
    process->wait_queue = NULL;
    process->state = PROCESS_SLEEPING;
    process_table_lock_release(flags);
    return 1;
}

/*
 * Bounded O(PROCESS_MAX) scan at the PIT rate. State transitions happen under
 * one table-lock acquisition; IPIs are sent only after releasing that lock.
 * A stale deadline can never wake a reused slot because reset clears it.
 */
void process_wake_expired(uint64_t now_ticks)
{
    uint32_t target_mask = 0U;
    int has_unpinned_wakeup = 0;

    if (!process_initialized_state || now_ticks == 0U)
        return;

    /*
     * Make all due sleepers runnable in one table-lock transaction. Record
     * destinations, then send at most one reschedule IPI per CPU after
     * dropping the lock. Unpinned work needs only one initial nudge: any CPU
     * can claim it on a later scheduling pass.
     */
    uint64_t flags = process_table_lock_acquire();
    for (uint32_t pid = 1U; pid < PROCESS_MAX; pid++)
    {
        process_t* process = &process_table_records[pid];
        if (process->state != PROCESS_SLEEPING ||
            process->sleep_deadline_ticks == 0U ||
            process->sleep_deadline_ticks > now_ticks)
            continue;

        process->state = PROCESS_READY;
        process->sleep_deadline_ticks = 0U;

        uint32_t target = process->owner_cpu;
        if (target == PROCESS_CPU_UNOWNED)
            target = process->affinity_cpu;

        if (target < CPU_LOCAL_MAX)
            target_mask |= 1U << target;
        else if (target == PROCESS_CPU_ANY)
            has_unpinned_wakeup = 1;
    }
    process_table_lock_release(flags);

    if (has_unpinned_wakeup)
    {
        uint32_t target = process_choose_wakeup_cpu(PROCESS_CPU_UNOWNED);
        if (target < CPU_LOCAL_MAX)
            target_mask |= 1U << target;
    }

    for (uint32_t cpu = 0U; cpu < CPU_LOCAL_MAX; cpu++)
    {
        if ((target_mask & (1U << cpu)) != 0U)
            process_request_cpu_reschedule(cpu);
    }
}


void process_wait_queue_init(process_wait_queue_t* queue)
{
    if (!queue)
        return;
    spinlock_init(&queue->lock);
    for (uint32_t i = 0U;
         i < (PROCESS_MAX + 31U) / 32U; i++)
        __atomic_store_n(&queue->waiters[i], 0U, __ATOMIC_RELAXED);
}

/*
 * Register and block while holding queue->lock across the process-state
 * transition. A concurrent waker therefore cannot slip between registration
 * and PROCESS_SLEEPING. This function returns after the task is scheduled
 * again; callers must always re-evaluate their wait condition.
 */
int process_wait_queue_sleep(process_wait_queue_t* queue)
{
    uint64_t interrupt_flags;
    if (!queue)
        return 0;

    __asm__ volatile ("pushfq; popq %0" : "=r"(interrupt_flags));
    if ((interrupt_flags & (1ULL << 9)) == 0U)
        return 0;

    uint64_t queue_flags = spinlock_lock_irqsave(&queue->lock);
    uint64_t process_flags = process_table_lock_acquire();
    uint32_t pid = process_current_slot();
    if (pid == 0U || pid >= PROCESS_MAX ||
        process_is_idle_locked(pid) ||
        process_table_records[pid].state != PROCESS_RUNNING ||
        process_table_records[pid].wait_queue != NULL)
    {
        process_table_lock_release(process_flags);
        spinlock_unlock_irqrestore(&queue->lock, queue_flags);
        return 0;
    }

    process_t* process = &process_table_records[pid];
    process->sleep_deadline_ticks = 0U;
    process->wait_queue = queue;
    process->state = PROCESS_SLEEPING;
    queue->waiters[pid / 32U] |= 1U << (pid % 32U);

    process_table_lock_release(process_flags);
    spinlock_unlock_irqrestore(&queue->lock, queue_flags);

    scheduler_request_reschedule();
    /* IF was enabled on entry; STI;HLT closes the lost-wakeup window. */
    __asm__ volatile ("sti; hlt" ::: "memory");
    return 1;
}

/* Wake the first live waiter, discarding stale PID bits safely. */
uint32_t process_wait_queue_wake_one(process_wait_queue_t* queue)
{
    if (!queue)
        return 0U;

    uint32_t awakened_pid = 0U;
    uint32_t owner_cpu = PROCESS_CPU_UNOWNED;
    uint32_t affinity_cpu = PROCESS_CPU_ANY;
    uint64_t queue_flags = spinlock_lock_irqsave(&queue->lock);

    for (uint32_t pid = 1U; pid < PROCESS_MAX; pid++)
    {
        uint32_t mask = 1U << (pid % 32U);
        uint32_t* word = &queue->waiters[pid / 32U];
        if ((__atomic_load_n(word, __ATOMIC_RELAXED) & mask) == 0U)
            continue;

        *word &= ~mask;
        uint64_t process_flags = process_table_lock_acquire();
        process_t* process = &process_table_records[pid];
        if (process->state == PROCESS_SLEEPING &&
            process->wait_queue == queue)
        {
            process->state = PROCESS_READY;
            process->wait_queue = NULL;
            process->sleep_deadline_ticks = 0U;
            owner_cpu = process->owner_cpu;
            affinity_cpu = process->affinity_cpu;
            awakened_pid = pid;
        }
        process_table_lock_release(process_flags);
        if (awakened_pid != 0U)
            break;
    }

    spinlock_unlock_irqrestore(&queue->lock, queue_flags);
    if (awakened_pid != 0U)
        process_send_wake_ipi(owner_cpu, affinity_cpu);
    return awakened_pid;
}

uint32_t process_wait_queue_wake_all(process_wait_queue_t* queue)
{
    if (!queue)
        return 0U;

    uint32_t count = 0U;
    while (count < PROCESS_MAX &&
           process_wait_queue_wake_one(queue) != 0U)
        count++;
    return count;
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
        process->state == PROCESS_ZOMBIE ||
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
        uint32_t target = process_choose_wakeup_cpu(owner_cpu);
        if (target != PROCESS_CPU_UNOWNED)
            process_request_cpu_reschedule(target);
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
    {
        process_table_records[current_pid].sleep_deadline_ticks = 0U;
        process_table_records[current_pid].wait_queue = NULL;
        process_table_records[current_pid].state = PROCESS_SLEEPING;
    }
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
        process->state == PROCESS_ZOMBIE ||
        process->state == PROCESS_REAPING ||
        process_is_idle_locked(pid))
    {
        process_table_lock_release(flags);
        return 0;
    }

    process->state = PROCESS_TERMINATED;
    process->exit_status = -1;
    int is_current = pid == process_current_slot();
    process_table_lock_release(flags);

    if (is_current) {
        /*
         * The current task still owns its stack, so no reaper can recycle
         * this PID yet. Revoke its IPC capabilities before returning to the
         * scheduler; non-current tasks are cleaned by process_reap().
         */
        ipc_process_cleanup(pid);
        return 1;
    }
    return process_reap(pid);
}

void process_terminate_current(void)
{
    process_terminate_current_with_status(0);
}

void process_terminate_current_with_status(int32_t exit_status)
{
    if (!process_initialized_state)
        return;
    uint32_t terminated_pid = 0U;
    uint64_t flags = process_table_lock_acquire();
    uint32_t current_pid = process_current_slot();
    if (current_pid != 0 && current_pid < PROCESS_MAX &&
        process_table_records[current_pid].state != PROCESS_UNUSED &&
        !process_is_idle_locked(current_pid))
    {
        process_table_records[current_pid].state = PROCESS_TERMINATED;
        process_table_records[current_pid].exit_status = exit_status;
        terminated_pid = current_pid;
    }
    process_table_lock_release(flags);

    /*
     * The running task cannot be reaped/reused until it relinquishes this
     * CPU's stack, so cleanup after dropping the process-table lock cannot
     * accidentally erase capabilities created by a new process with the
     * same PID.
     */
    if (terminated_pid != 0U)
        ipc_process_cleanup(terminated_pid);
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
    if (pid == process_current_slot() || process_is_idle_locked(pid) ||
        process->owner_cpu != PROCESS_CPU_UNOWNED)
    {
        process_table_lock_release(flags);
        return 0;
    }

    if (process->state == PROCESS_ZOMBIE)
    {
        uint32_t parent_pid = process->parent_pid;
        /*
         * A zombie belonging to a live user parent is waitable state, not
         * leaked execution resources. Generic reaper passes must not consume
         * it before waitpid observes the exit status; only orphaned zombies
         * may be reclaimed here.
         */
        if (parent_pid > 0U && parent_pid < PROCESS_MAX)
        {
            const process_t* parent = &process_table_records[parent_pid];
            if (parent->is_user &&
                parent->state != PROCESS_UNUSED &&
                parent->state != PROCESS_TERMINATED &&
                parent->state != PROCESS_ZOMBIE &&
                parent->state != PROCESS_REAPING)
            {
                process_table_lock_release(flags);
                return 0;
            }
        }

        process_reparent_children_locked(pid);
        process_reset_record(process);
        process->pid = pid;
        process_table_lock_release(flags);
        return 1;
    }

    if (process->state != PROCESS_TERMINATED)
    {
        process_table_lock_release(flags);
        return 0;
    }

    snapshot = *process;
    process->state = PROCESS_REAPING;
    process_table_lock_release(flags);

    ipc_process_cleanup(pid);

    if (!process_release_user_resources(&snapshot))
    {
        flags = process_table_lock_acquire();
        process = &process_table_records[pid];
        uint32_t parent_pid = process->parent_pid;
        int wake_parent = parent_pid > 0U && parent_pid < PROCESS_MAX &&
            process_table_records[parent_pid].is_user &&
            process_table_records[parent_pid].state == PROCESS_SLEEPING;
        if (process->state == PROCESS_REAPING)
            process->state = PROCESS_TERMINATED;
        process_table_lock_release(flags);
        if (wake_parent)
            (void)process_wake(parent_pid);
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
    uint32_t parent_pid = process->parent_pid;
    int wake_parent = parent_pid > 0U && parent_pid < PROCESS_MAX &&
        process_table_records[parent_pid].is_user &&
        process_table_records[parent_pid].state == PROCESS_SLEEPING;
    process_reparent_children_locked(pid);
    process_reset_record(process);
    process->pid = pid;
    process_table_lock_release(flags);
    if (wake_parent)
        (void)process_wake(parent_pid);
    return 1;
}

int process_wait_child(uint32_t parent_pid, uint32_t child_pid,
                       int32_t* exit_status_out)
{
    if (!process_initialized_state || parent_pid == 0U ||
        parent_pid >= PROCESS_MAX || child_pid == 0U ||
        child_pid >= PROCESS_MAX || parent_pid == child_pid)
        return -1;

    uint64_t flags = process_table_lock_acquire();
    process_t* parent = &process_table_records[parent_pid];
    process_t* child = &process_table_records[child_pid];
    if (parent_pid != process_current_slot() || !parent->is_user ||
        parent->state == PROCESS_UNUSED ||
        parent->state == PROCESS_TERMINATED ||
        parent->state == PROCESS_ZOMBIE ||
        parent->state == PROCESS_REAPING ||
        parent->owner_cpu != process_current_cpu_id() ||
        child->state == PROCESS_UNUSED ||
        child->parent_pid != parent_pid)
    {
        process_table_lock_release(flags);
        return -1;
    }

    if (child->state == PROCESS_ZOMBIE &&
        child->owner_cpu == PROCESS_CPU_UNOWNED)
    {
        int32_t status = child->exit_status;
        process_reparent_children_locked(child_pid);
        process_reset_record(child);
        child->pid = child_pid;
        process_table_lock_release(flags);
        if (exit_status_out)
            *exit_status_out = status;
        return (int)child_pid;
    }

    if (child->state == PROCESS_REAPING ||
        (child->state == PROCESS_TERMINATED &&
         child->owner_cpu == PROCESS_CPU_UNOWNED))
    {
        process_table_lock_release(flags);
        return -1;
    }

    /*
     * Publish the sleeping state under the same lock used by child exit.
     * This closes the lost-wakeup window between checking and blocking.
     */
    parent->state = PROCESS_SLEEPING;
    process_table_lock_release(flags);
    return 0;
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
            process_table_records[i].state != PROCESS_ZOMBIE &&
            process_table_records[i].state != PROCESS_REAPING)
            count++;
    process_table_lock_release(flags);
    return count;
}

uint32_t process_current_pid(void)
{
    return process_current_slot();
}
