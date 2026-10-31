/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "semaphore.h"

#include <assert.h>
#include <stdio.h>

static uint32_t current_pid = 0U;
static uint64_t fake_ticks;

uint32_t process_current_pid(void)
{
    return current_pid;
}

uint64_t timer_get_ticks64(void) { return fake_ticks; }

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

int process_wait_queue_sleep_locked(process_wait_queue_t* queue, uint64_t flags)
{
    (void)queue;
    (void)flags;
    return 0;
}

int process_wait_queue_sleep_until_locked(process_wait_queue_t* queue,
                                          uint64_t flags,
                                          uint64_t deadline_ticks)
{
    (void)queue;
    (void)flags;
    fake_ticks = deadline_ticks;
    return 1;
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
    kernel_semaphore_t semaphore;
    assert(!kernel_semaphore_init(NULL, 0U, 1U));
    assert(!kernel_semaphore_init(&semaphore, 2U, 1U));
    assert(!kernel_semaphore_init(&semaphore, 0U, 0U));

    assert(kernel_semaphore_init(&semaphore, 1U, 3U));
    assert(semaphore.count == 1U);
    assert(semaphore.maximum == 3U);
    assert(kernel_semaphore_try_wait(&semaphore));
    assert(semaphore.count == 0U);
    assert(!kernel_semaphore_try_wait(&semaphore));

    assert(kernel_semaphore_post(&semaphore));
    assert(semaphore.count == 1U);
    assert(kernel_semaphore_post(&semaphore));
    assert(kernel_semaphore_post(&semaphore));
    assert(semaphore.count == 3U);
    assert(!kernel_semaphore_post(&semaphore));
    assert(semaphore.count == 3U);

    current_pid = 7U;
    fake_ticks = 0U;
    assert(kernel_semaphore_wait_until(&semaphore, 10U));
    assert(semaphore.count == 2U);
    assert(!kernel_semaphore_wait_until(&semaphore, 20U));
    assert(fake_ticks == 20U);
    assert(!kernel_semaphore_wait_until(&semaphore, 0U));
    current_pid = 0U;
    /* Blocking requires a scheduled process, which this host test lacks. */
    assert(!kernel_semaphore_wait(&semaphore));
    assert(!kernel_semaphore_wait(NULL));
    assert(!kernel_semaphore_try_wait(NULL));
    assert(!kernel_semaphore_post(NULL));

    puts("VANTAOS_SEMAPHORE_API_OK");
    return 0;
}