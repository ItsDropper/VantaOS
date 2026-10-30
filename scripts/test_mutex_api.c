/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "mutex.h"

#include <assert.h>
#include <stdio.h>

static uint32_t current_pid;
static uint32_t wake_one_calls;
static int simulate_handoff;

uint32_t process_current_pid(void)
{
    return current_pid;
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
    (void)flags;
    if (!simulate_handoff)
        return 0;

    /*
     * Model another owner releasing while this process is registered and
     * asleep. The real wait-queue implementation provides this handoff.
     */
    ((kernel_mutex_t*)((char*)queue -
        __builtin_offsetof(kernel_mutex_t, waiters)))->owner_pid = 0U;
    simulate_handoff = 0;
    return 1;
}

uint32_t process_wait_queue_wake_one(process_wait_queue_t* queue)
{
    (void)queue;
    wake_one_calls++;
    return 1U;
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
    current_pid = 0U;
    assert(!kernel_mutex_try_lock(&dynamic));
    assert(!kernel_mutex_lock(&dynamic));
    assert(!kernel_mutex_unlock(&dynamic));
    assert(!kernel_mutex_try_lock(NULL));
    assert(!kernel_mutex_lock(NULL));
    assert(!kernel_mutex_unlock(NULL));

    current_pid = 7U;
    assert(kernel_mutex_try_lock(&dynamic));
    assert(dynamic.owner_pid == 7U);
    assert(!kernel_mutex_try_lock(&dynamic));
    assert(!kernel_mutex_lock(&dynamic)); /* Non-recursive. */

    current_pid = 8U;
    assert(!kernel_mutex_unlock(&dynamic)); /* Non-owner cannot unlock. */
    assert(dynamic.owner_pid == 7U);
    assert(wake_one_calls == 0U);

    current_pid = 7U;
    assert(kernel_mutex_unlock(&dynamic));
    assert(dynamic.owner_pid == 0U);
    assert(wake_one_calls == 1U);

    /* Blocking lock retries ownership after an advisory wakeup. */
    current_pid = 8U;
    dynamic.owner_pid = 7U;
    simulate_handoff = 1;
    assert(kernel_mutex_lock(&dynamic));
    assert(dynamic.owner_pid == 8U);
    assert(kernel_mutex_unlock(&dynamic));
    assert(wake_one_calls == 2U);

    /* Failed wait registration must not steal an occupied mutex. */
    current_pid = 9U;
    dynamic.owner_pid = 7U;
    simulate_handoff = 0;
    assert(!kernel_mutex_lock(&dynamic));
    assert(dynamic.owner_pid == 7U);

    puts("VANTAOS_MUTEX_API_OK");
    return 0;
}
