/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef KERNEL_MUTEX_H
#define KERNEL_MUTEX_H

#include "process.h"
#include "timer.h"

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

/*
 * Acquire before an absolute PIT-tick deadline. Availability is checked
 * before timeout while holding the same lock that serializes ownership.
 * Returns zero on timeout, invalid context, or recursive acquisition.
 */
static inline int kernel_mutex_lock_until(kernel_mutex_t* mutex,
                                          uint64_t deadline_ticks)
{
    if (!mutex || deadline_ticks == 0U)
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

        if (timer_get_ticks64() >= deadline_ticks)
        {
            process_wait_queue_unlock(&mutex->waiters, flags);
            return 0;
        }

        if (!process_wait_queue_sleep_until_locked(&mutex->waiters, flags,
                                                   deadline_ticks))
            return 0;
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