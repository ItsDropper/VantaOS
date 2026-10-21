/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "process_internal.h"

/*
 * Exception handlers must not spin indefinitely waiting for the process-table
 * lock. If another CPU owns it, the caller must take its fatal fallback path.
 */
int process_terminate_current_try_with_status(int32_t exit_status)
{
    if (!process_initialized_state)
        return 0;

    uint64_t flags;
    if (!spinlock_try_lock_irqsave(&process_table_lock, &flags))
        return 0;

    uint32_t current_pid = process_current_slot();
    int terminated = 0;
    if (current_pid != 0U && current_pid < PROCESS_MAX &&
        process_table_records[current_pid].state != PROCESS_UNUSED &&
        !process_is_idle_locked(current_pid))
    {
        process_table_records[current_pid].state = PROCESS_TERMINATED;
        process_table_records[current_pid].exit_status = exit_status;
        terminated = 1;
    }

    process_table_lock_release(flags);
    return terminated;
}
