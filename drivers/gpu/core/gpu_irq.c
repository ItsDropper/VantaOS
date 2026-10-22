/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_irq.h"

#include <string.h>

static int gpu_irq_is_power_of_two(size_t value)
{
    return value >= 2U && (value & (value - 1U)) == 0U;
}

int gpu_irq_queue_invariant(const gpu_irq_queue_t *queue)
{
    uint64_t queued;
    if (queue == NULL || queue->initialized == 0U ||
        queue->events == NULL || !gpu_irq_is_power_of_two(queue->capacity))
        return 0;
    {
        uint64_t producer = __atomic_load_n(&queue->producer_sequence,
                                            __ATOMIC_ACQUIRE);
        uint64_t consumer = __atomic_load_n(&queue->consumer_sequence,
                                            __ATOMIC_ACQUIRE);
        queued = producer - consumer;
        return producer >= consumer && queued <= queue->capacity;
    }
}

gpu_irq_status_t gpu_irq_queue_init(gpu_irq_queue_t *queue,
                                    gpu_irq_event_t *storage,
                                    size_t capacity)
{
    if (queue == NULL || storage == NULL ||
        !gpu_irq_is_power_of_two(capacity) ||
        capacity > (size_t)(UINT64_MAX / 2U))
        return GPU_IRQ_INVALID_ARGUMENT;
    memset(queue, 0, sizeof(*queue));
    memset(storage, 0, capacity * sizeof(*storage));
    queue->events = storage;
    queue->capacity = capacity;
    queue->initialized = 1U;
    return GPU_IRQ_OK;
}

gpu_irq_status_t gpu_irq_queue_enable(gpu_irq_queue_t *queue)
{
    if (!gpu_irq_queue_invariant(queue))
        return GPU_IRQ_INVALID_ARGUMENT;
    if (__atomic_load_n(&queue->device_lost, __ATOMIC_ACQUIRE) != 0U)
        return GPU_IRQ_DEVICE_LOST;
    __atomic_store_n(&queue->enabled, 1U, __ATOMIC_RELEASE);
    return GPU_IRQ_OK;
}

void gpu_irq_queue_disable(gpu_irq_queue_t *queue)
{
    if (queue != NULL)
        __atomic_store_n(&queue->enabled, 0U, __ATOMIC_RELEASE);
}

gpu_irq_status_t gpu_irq_push(gpu_irq_queue_t *queue,
                              const gpu_irq_event_t *event)
{
    uint64_t queued;
    gpu_irq_event_t copy;

    if (queue == NULL || event == NULL)
        return GPU_IRQ_INVALID_ARGUMENT;
    if (!gpu_irq_queue_invariant(queue))
        return GPU_IRQ_NOT_INITIALIZED;
    if (__atomic_load_n(&queue->enabled, __ATOMIC_ACQUIRE) == 0U)
        return GPU_IRQ_NOT_READY;
    if (__atomic_load_n(&queue->device_lost, __ATOMIC_ACQUIRE) != 0U)
        return GPU_IRQ_DEVICE_LOST;
    if (event->type < GPU_IRQ_EVENT_COMPLETION ||
        event->type > GPU_IRQ_EVENT_DEVICE_LOST)
        return GPU_IRQ_INVALID_ARGUMENT;
    {
        uint64_t producer = __atomic_load_n(&queue->producer_sequence,
                                            __ATOMIC_RELAXED);
        uint64_t consumer = __atomic_load_n(&queue->consumer_sequence,
                                            __ATOMIC_ACQUIRE);
        if (producer > UINT64_MAX - queue->capacity) {
            __atomic_store_n(&queue->device_lost, 1U, __ATOMIC_RELEASE);
            __atomic_store_n(&queue->enabled, 0U, __ATOMIC_RELEASE);
            return GPU_IRQ_OVERFLOW;
        }
        queued = producer - consumer;
    }
    if (queued >= queue->capacity) {
        (void)__atomic_add_fetch(&queue->dropped_events, 1U,
                                 __ATOMIC_RELAXED);
        if (event->type == GPU_IRQ_EVENT_DEVICE_LOST) {
            __atomic_store_n(&queue->device_lost, 1U, __ATOMIC_RELEASE);
            __atomic_store_n(&queue->enabled, 0U, __ATOMIC_RELEASE);
        }
        return GPU_IRQ_FULL;
    }

    copy = *event;
    {
        uint64_t producer = __atomic_load_n(&queue->producer_sequence,
                                            __ATOMIC_RELAXED);
        copy.sequence = producer;
        queue->events[(size_t)(producer &
                               (uint64_t)(queue->capacity - 1U))] = copy;
        __atomic_store_n(&queue->producer_sequence, producer + 1U,
                         __ATOMIC_RELEASE);
    }
    if (event->type == GPU_IRQ_EVENT_DEVICE_LOST) {
        __atomic_store_n(&queue->device_lost, 1U, __ATOMIC_RELEASE);
        __atomic_store_n(&queue->enabled, 0U, __ATOMIC_RELEASE);
    }
    return GPU_IRQ_OK;
}

gpu_irq_status_t gpu_irq_pop(gpu_irq_queue_t *queue,
                             gpu_irq_event_t *event)
{
    uint64_t queued;
    if (queue == NULL || event == NULL)
        return GPU_IRQ_INVALID_ARGUMENT;
    if (!gpu_irq_queue_invariant(queue))
        return GPU_IRQ_NOT_INITIALIZED;
    {
        uint64_t consumer = __atomic_load_n(&queue->consumer_sequence,
                                            __ATOMIC_RELAXED);
        uint64_t producer = __atomic_load_n(&queue->producer_sequence,
                                            __ATOMIC_ACQUIRE);
        queued = producer - consumer;
        if (queued == 0U)
            return GPU_IRQ_EMPTY;
        *event = queue->events[(size_t)(consumer &
                                        (uint64_t)(queue->capacity - 1U))];
        __atomic_store_n(&queue->consumer_sequence, consumer + 1U,
                         __ATOMIC_RELEASE);
    }
    return GPU_IRQ_OK;
}

gpu_irq_status_t gpu_irq_drain(gpu_irq_queue_t *queue, size_t max_events,
                               gpu_irq_dispatch_fn dispatch, void *context,
                               size_t *out_processed)
{
    size_t processed = 0U;
    if (out_processed == NULL || dispatch == NULL)
        return GPU_IRQ_INVALID_ARGUMENT;
    *out_processed = 0U;
    if (queue == NULL || !gpu_irq_queue_invariant(queue))
        return GPU_IRQ_NOT_INITIALIZED;

    while (processed < max_events) {
        gpu_irq_event_t event;
        gpu_irq_status_t status = gpu_irq_pop(queue, &event);
        if (status == GPU_IRQ_EMPTY)
            break;
        if (status != GPU_IRQ_OK)
            return status;
        ++processed;
        if (!dispatch(context, &event)) {
            __atomic_store_n(&queue->device_lost, 1U, __ATOMIC_RELEASE);
            __atomic_store_n(&queue->enabled, 0U, __ATOMIC_RELEASE);
            *out_processed = processed;
            return GPU_IRQ_DISPATCH_FAILED;
        }
    }
    *out_processed = processed;
    return GPU_IRQ_OK;
}

gpu_irq_status_t gpu_irq_mark_device_lost(gpu_irq_queue_t *queue)
{
    if (queue == NULL || !gpu_irq_queue_invariant(queue))
        return GPU_IRQ_INVALID_ARGUMENT;
    __atomic_store_n(&queue->device_lost, 1U, __ATOMIC_RELEASE);
    __atomic_store_n(&queue->enabled, 0U, __ATOMIC_RELEASE);
    return GPU_IRQ_OK;
}

gpu_irq_status_t gpu_irq_reset(gpu_irq_queue_t *queue)
{
    if (queue == NULL || !gpu_irq_queue_invariant(queue))
        return GPU_IRQ_INVALID_ARGUMENT;
    if (__atomic_load_n(&queue->enabled, __ATOMIC_ACQUIRE) != 0U)
        return GPU_IRQ_NOT_READY;
    memset(queue->events, 0, queue->capacity * sizeof(queue->events[0]));
    __atomic_store_n(&queue->producer_sequence, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&queue->consumer_sequence, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&queue->dropped_events, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&queue->device_lost, 0U, __ATOMIC_RELEASE);
    return GPU_IRQ_OK;
}
