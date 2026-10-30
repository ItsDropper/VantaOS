/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef KERNEL_BARRIER_H
#define KERNEL_BARRIER_H

#include "process.h"

/*
 * Reusable process barrier. The wait-queue lock protects both the arrival
 * count and generation, making the predicate check and waiter registration
 * atomic with respect to the final participant releasing the generation.
 */
typedef struct
{
    process_wait_queue_t waiters;
    uint32_t threshold;
    uint32_t arrived;
    uint64_t generation;
} kernel_barrier_t;

static inline int kernel_barrier_init(kernel_barrier_t* barrier,
                                      uint32_t participants)
{
    if (!barrier || participants == 0U)
        return 0;

    process_wait_queue_init(&barrier->waiters);
    barrier->threshold = participants;
    barrier->arrived = 0U;
    barrier->generation = 0U;
    return 1;
}

/*
 * Returns one after the caller passes the barrier, zero for invalid context.
 * Every participant must call once per generation. A failed wait does not
 * consume an arrival slot.
 */
static inline int kernel_barrier_wait(kernel_barrier_t* barrier)
{
    if (!barrier || barrier->threshold == 0U)
        return 0;

    uint32_t pid = process_current_pid();
    if (pid == 0U || pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_wait_queue_lock(&barrier->waiters);
    uint64_t generation = barrier->generation;

    if (barrier->arrived >= barrier->threshold)
    {
        process_wait_queue_unlock(&barrier->waiters, flags);
        return 0;
    }

    barrier->arrived++;
    if (barrier->arrived == barrier->threshold)
    {
        barrier->arrived = 0U;
        barrier->generation++;
        process_wait_queue_unlock(&barrier->waiters, flags);
        (void)process_wait_queue_wake_all(&barrier->waiters);
        return 1;
    }

    while (barrier->generation == generation)
    {
        if (!process_wait_queue_sleep_locked(&barrier->waiters, flags))
        {
            /*
             * A failed block must not strand an arrival in this generation.
             * If other participants completed the generation meanwhile, our
             * arrival was already consumed and this wait succeeded.
             */
            flags = process_wait_queue_lock(&barrier->waiters);
            int completed = barrier->generation != generation;
            if (!completed && barrier->arrived != 0U)
                barrier->arrived--;
            process_wait_queue_unlock(&barrier->waiters, flags);
            return completed;
        }
        flags = process_wait_queue_lock(&barrier->waiters);
    }

    process_wait_queue_unlock(&barrier->waiters, flags);
    return 1;
}

#endif /* KERNEL_BARRIER_H */
