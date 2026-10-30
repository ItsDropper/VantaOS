/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "mutex.h"

#include <assert.h>
#include <stdio.h>

uint32_t process_current_pid(void)
{
    return 0U; /* Host test has no scheduled process context. */
}

void process_wait_queue_init(process_wait_queue_t* queue)
{
    spinlock_init(&queue->lock);
    for (uint32_t i = 0U; i < (PROCESS_MAX + 31U) / 32U; i++)
        queue->waiters[i] = 0U;
}

uint64_t process_wait_queue_lock(process_wait_queue_t* queue)
{
    (void)queue;
    return 0U;
}

void process_wait_queue_unlock(process_wait_queue_t* queue, uint64_t flags)
{
    (void)queue;
    (void)flags;
}

int process_wait_queue_sleep_locked(process_wait_queue_t* queue,
                                    uint64_t flags)
{
    (void)queue;
    (void)flags;
    return 0;
}

uint32_t process_wait_queue_wake_one(process_wait_queue_t* queue)
{
    (void)queue;
    return 0U;
}

uint32_t process_wait_queue_wake_all(process_wait_queue_t* queue)
{
    (void)queue;
    return 0U;
}

int main(void)
{
    kernel_mutex_t initialized = KERNEL_MUTEX_INITIALIZER;
    kernel_mutex_t dynamic;

    assert(initialized.owner_pid == 0U);
    kernel_mutex_init(&dynamic);
    assert(dynamic.owner_pid == 0U);
    for (uint32_t i = 0U; i < (PROCESS_MAX + 31U) / 32U; i++)
        assert(dynamic.waiters.waiters[i] == 0U);

    /* PID zero is the scheduler/idle context, not a lock owner. */
    assert(!kernel_mutex_try_lock(&dynamic));
    assert(!kernel_mutex_lock(&dynamic));
    assert(!kernel_mutex_unlock(&dynamic));
    assert(!kernel_mutex_try_lock(NULL));
    assert(!kernel_mutex_lock(NULL));
    assert(!kernel_mutex_unlock(NULL));

    puts("VANTAOS_MUTEX_API_OK");
    return 0;
}