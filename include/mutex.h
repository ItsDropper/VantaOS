/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef KERNEL_MUTEX_H
#define KERNEL_MUTEX_H

#include "process.h"

/*
 * Sleeping, non-recursive mutex. Ownership is a PID and all ownership
 * transitions are serialized by the embedded wait-queue lock.
 */
typedef struct
{
    process_wait_queue_t waiters;
    uint32_t owner_pid;
} kernel_mutex_t;

#define KERNEL_MUTEX_INITIALIZER { { SPINLOCK_INITIALIZER, { 0U } }, 0U }

static inline void kernel_mutex_init(kernel_mutex_t* mutex)
{
    if (!mutex)
        return;
    process_wait_queue_init(&mutex->waiters);
    mutex->owner_pid = 0U;
}

static inline int kernel_mutex_try_lock(kernel_mutex_t* mutex)
{
    if (!mutex)
        return 0;

    uint32_t pid = process_current_pid();
    if (pid == 0U || pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_wait_queue_lock(&mutex->waiters);
    int acquired = mutex->owner_pid == 0U;
    if (acquired)
        mutex->owner_pid = pid;
    process_wait_queue_unlock(&mutex->waiters, flags);
    return acquired;
}

/* Returns one when acquired, zero for invalid context or recursive use. */
static inline int kernel_mutex_lock(kernel_mutex_t* mutex)
{
    if (!mutex)
        return 0;

    uint32_t pid = process_current_pid();
    if (pid == 0U || pid >= PROCESS_MAX)
        return 0;

    for (;;)
    {
        uint64_t flags = process_wait_queue_lock(&mutex->waiters);
        if (mutex->owner_pid == 0U)
        {
            mutex->owner_pid = pid;
            process_wait_queue_unlock(&mutex->waiters, flags);
            return 1;
        }

        if (mutex->owner_pid == pid)
        {
            process_wait_queue_unlock(&mutex->waiters, flags);
            return 0;
        }

        /* The locked sleep consumes flags and releases the queue lock. */
        if (!process_wait_queue_sleep_locked(&mutex->waiters, flags))
            return 0;
        /* A wakeup is advisory; retry ownership under the queue lock. */
    }
}

static inline int kernel_mutex_unlock(kernel_mutex_t* mutex)
{
    if (!mutex)
        return 0;

    uint32_t pid = process_current_pid();
    if (pid == 0U || pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_wait_queue_lock(&mutex->waiters);
    if (mutex->owner_pid != pid)
    {
        process_wait_queue_unlock(&mutex->waiters, flags);
        return 0;
    }

    mutex->owner_pid = 0U;
    process_wait_queue_unlock(&mutex->waiters, flags);
    (void)process_wait_queue_wake_one(&mutex->waiters);
    return 1;
}

#endif /* KERNEL_MUTEX_H */