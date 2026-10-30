/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "barrier.h"

#include <assert.h>
#include <stdio.h>

static uint32_t wake_all_calls;

uint32_t process_current_pid(void) { return 7U; }
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
uint32_t process_wait_queue_wake_all(process_wait_queue_t* queue)
{
    (void)queue;
    wake_all_calls++;
    return 0U;
}

int main(void)
{
    kernel_barrier_t barrier;
    assert(!kernel_barrier_init(NULL, 1U));
    assert(!kernel_barrier_init(&barrier, 0U));
    assert(kernel_barrier_init(&barrier, 1U));
    assert(barrier.threshold == 1U);
    assert(barrier.arrived == 0U);
    assert(barrier.generation == 0U);

    /* A one-participant barrier releases immediately without sleeping. */
    assert(kernel_barrier_wait(&barrier));
    assert(barrier.arrived == 0U);
    assert(barrier.generation == 1U);
    assert(wake_all_calls == 1U);
    assert(!kernel_barrier_wait(NULL));
    assert(wake_all_calls == 1U);

    /* Failed blocking must roll back the caller's partial arrival. */
    assert(kernel_barrier_init(&barrier, 2U));
    assert(!kernel_barrier_wait(&barrier));
    assert(barrier.arrived == 0U);
    assert(barrier.generation == 0U);
    assert(wake_all_calls == 1U);

    puts("VANTAOS_BARRIER_API_OK");
    return 0;
}
