/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef KERNEL_SEMAPHORE_H
#define KERNEL_SEMAPHORE_H

#include "process.h"

/* Bounded counting semaphore for normal process context. */
typedef struct
{
    process_wait_queue_t waiters;
    uint32_t count;
    uint32_t maximum;
} kernel_semaphore_t;

static inline int kernel_semaphore_init(kernel_semaphore_t* semaphore,
                                        uint32_t initial_count,
                                        uint32_t maximum)
{
    if (!semaphore || maximum == 0U || initial_count > maximum)
        return 0;
    process_wait_queue_init(&semaphore->waiters);
    semaphore->count = initial_count;
    semaphore->maximum = maximum;
    return 1;
}

static inline int kernel_semaphore_try_wait(kernel_semaphore_t* semaphore)
{
    if (!semaphore)
        return 0;
    uint64_t flags = process_wait_queue_lock(&semaphore->waiters);
    int acquired = semaphore->count != 0U;
    if (acquired)
        semaphore->count--;
    process_wait_queue_unlock(&semaphore->waiters, flags);
    return acquired;
}

static inline int kernel_semaphore_wait(kernel_semaphore_t* semaphore)
{
    if (!semaphore)
        return 0;
    uint32_t pid = process_current_pid();
    if (pid == 0U || pid >= PROCESS_MAX)
        return 0;

    for (;;)
    {
        uint64_t flags = process_wait_queue_lock(&semaphore->waiters);
        if (semaphore->count != 0U)
        {
            semaphore->count--;
            process_wait_queue_unlock(&semaphore->waiters, flags);
            return 1;
        }

        if (!process_wait_queue_sleep_locked(&semaphore->waiters, flags))
            return 0;
    }
}

static inline int kernel_semaphore_post(kernel_semaphore_t* semaphore)
{
    if (!semaphore)
        return 0;

    uint64_t flags = process_wait_queue_lock(&semaphore->waiters);
    if (semaphore->count >= semaphore->maximum)
    {
        process_wait_queue_unlock(&semaphore->waiters, flags);
        return 0;
    }
    semaphore->count++;
    process_wait_queue_unlock(&semaphore->waiters, flags);

    (void)process_wait_queue_wake_one(&semaphore->waiters);
    return 1;
}

#endif /* KERNEL_SEMAPHORE_H */