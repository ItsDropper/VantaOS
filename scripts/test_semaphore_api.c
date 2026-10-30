/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "semaphore.h"

#include <assert.h>
#include <stdio.h>

uint32_t process_current_pid(void)
{
    return 0U;
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

int process_wait_queue_sleep_locked(process_wait_queue_t* queue, uint64_t flags)
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

    /* Blocking requires a scheduled process, which this host test lacks. */
    assert(!kernel_semaphore_wait(&semaphore));
    assert(!kernel_semaphore_wait(NULL));
    assert(!kernel_semaphore_try_wait(NULL));
    assert(!kernel_semaphore_post(NULL));

    puts("VANTAOS_SEMAPHORE_API_OK");
    return 0;
}