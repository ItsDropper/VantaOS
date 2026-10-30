/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef KERNEL_EVENT_H
#define KERNEL_EVENT_H

#include "process.h"

/*
 * Process-context event. Manual-reset events remain signaled until reset and
 * release every waiter. Auto-reset events release one waiter and consume the
 * signal when a waiter successfully observes it.
 *
 * The state and waiter registration share the wait-queue lock, so set/wait
 * cannot lose a notification between checking the predicate and blocking.
 * All operations are process-context only; even set/reset/is_set may contend
 * on a lock held by a process interrupted on this CPU.
 */
typedef struct
{
    process_wait_queue_t waiters;
    uint32_t signaled;
    uint32_t manual_reset;
} kernel_event_t;

#define KERNEL_EVENT_INITIALIZER(manual, initial) \
    { { SPINLOCK_INITIALIZER, { 0U } }, ((initial) ? 1U : 0U), \
      ((manual) ? 1U : 0U) }

static inline void kernel_event_init(kernel_event_t* event,
                                     int manual_reset,
                                     int initially_signaled)
{
    if (!event)
        return;
    process_wait_queue_init(&event->waiters);
    event->signaled = initially_signaled ? 1U : 0U;
    event->manual_reset = manual_reset ? 1U : 0U;
}

static inline int kernel_event_wait(kernel_event_t* event)
{
    if (!event)
        return 0;

    uint32_t pid = process_current_pid();
    if (pid == 0U || pid >= PROCESS_MAX)
        return 0;

    for (;;)
    {
        uint64_t flags = process_wait_queue_lock(&event->waiters);
        if (event->signaled != 0U)
        {
            if (event->manual_reset == 0U)
                event->signaled = 0U;
            process_wait_queue_unlock(&event->waiters, flags);
            return 1;
        }

        if (!process_wait_queue_sleep_locked(&event->waiters, flags))
            return 0;
    }
}

/* Set persists until consumed (auto-reset) or reset (manual-reset). */
static inline int kernel_event_set(kernel_event_t* event)
{
    if (!event)
        return 0;

    uint64_t flags = process_wait_queue_lock(&event->waiters);
    event->signaled = 1U;
    uint32_t manual = event->manual_reset;
    process_wait_queue_unlock(&event->waiters, flags);

    if (manual)
        (void)process_wait_queue_wake_all(&event->waiters);
    else
        (void)process_wait_queue_wake_one(&event->waiters);
    return 1;
}

static inline int kernel_event_reset(kernel_event_t* event)
{
    if (!event)
        return 0;
    uint64_t flags = process_wait_queue_lock(&event->waiters);
    event->signaled = 0U;
    process_wait_queue_unlock(&event->waiters, flags);
    return 1;
}

static inline int kernel_event_is_set(kernel_event_t* event)
{
    if (!event)
        return 0;
    uint64_t flags = process_wait_queue_lock(&event->waiters);
    int signaled = event->signaled != 0U;
    process_wait_queue_unlock(&event->waiters, flags);
    return signaled;
}

#endif /* KERNEL_EVENT_H */
