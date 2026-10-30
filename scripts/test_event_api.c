/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "event.h"

#include <assert.h>
#include <stdio.h>

static uint32_t wake_one_calls;
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

uint32_t process_wait_queue_wake_one(process_wait_queue_t* queue)
{
    (void)queue;
    wake_one_calls++;
    return 7U;
}

uint32_t process_wait_queue_wake_all(process_wait_queue_t* queue)
{
    (void)queue;
    wake_all_calls++;
    return 1U;
}

int main(void)
{
    kernel_event_t automatic;
    kernel_event_init(&automatic, 0, 0);
    assert(!kernel_event_is_set(&automatic));
    assert(!kernel_event_wait(&automatic));
    assert(kernel_event_set(&automatic));
    assert(kernel_event_is_set(&automatic));
    assert(kernel_event_wait(&automatic));
    assert(!kernel_event_is_set(&automatic));
    assert(!kernel_event_wait(&automatic));
    assert(wake_one_calls == 1U);
    assert(wake_all_calls == 0U);

    kernel_event_t manual = KERNEL_EVENT_INITIALIZER(1, 1);
    assert(kernel_event_wait(&manual));
    assert(kernel_event_wait(&manual));
    assert(kernel_event_is_set(&manual));
    assert(kernel_event_reset(&manual));
    assert(!kernel_event_is_set(&manual));
    assert(!kernel_event_wait(&manual));
    assert(kernel_event_set(&manual));
    assert(wake_all_calls == 1U);
    assert(kernel_event_wait(&manual));
    assert(kernel_event_wait(&manual));
    assert(kernel_event_reset(&manual));
    assert(!kernel_event_set(NULL));
    assert(!kernel_event_reset(NULL));
    assert(!kernel_event_is_set(NULL));
    assert(!kernel_event_wait(NULL));

    puts("VANTAOS_EVENT_API_OK");
    return 0;
}
