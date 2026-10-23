/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_IRQ_H
#define VANTA_GPU_IRQ_H

#include <stddef.h>
#include <stdint.h>

typedef enum gpu_irq_event_type {
    GPU_IRQ_EVENT_COMPLETION = 1,
    GPU_IRQ_EVENT_FAULT,
    GPU_IRQ_EVENT_VBLANK,
    GPU_IRQ_EVENT_DEVICE_LOST
} gpu_irq_event_type_t;

typedef enum gpu_irq_status {
    GPU_IRQ_OK = 0,
    GPU_IRQ_INVALID_ARGUMENT,
    GPU_IRQ_NOT_INITIALIZED,
    GPU_IRQ_NOT_READY,
    GPU_IRQ_EMPTY,
    GPU_IRQ_FULL,
    GPU_IRQ_DEVICE_LOST,
    GPU_IRQ_OVERFLOW,
    GPU_IRQ_DISPATCH_FAILED
} gpu_irq_status_t;

typedef struct gpu_irq_event {
    uint64_t sequence;
    uint64_t fence;
    uint32_t type;
    uint32_t status;
    uint64_t data0;
    uint64_t data1;
} gpu_irq_event_t;

typedef struct gpu_irq_queue {
    gpu_irq_event_t *events;
    size_t capacity;
    uint64_t producer_sequence;
    uint64_t consumer_sequence;
    uint64_t dropped_events;
    uint8_t initialized;
    uint8_t enabled;
    uint8_t device_lost;
} gpu_irq_queue_t;

typedef int (*gpu_irq_dispatch_fn)(void *context,
                                   const gpu_irq_event_t *event);

/* Single producer (IRQ path), single consumer (deferred worker) queue. */
gpu_irq_status_t gpu_irq_queue_init(gpu_irq_queue_t *queue,
                                    gpu_irq_event_t *storage,
                                    size_t capacity);
gpu_irq_status_t gpu_irq_queue_enable(gpu_irq_queue_t *queue);
void gpu_irq_queue_disable(gpu_irq_queue_t *queue);
gpu_irq_status_t gpu_irq_push(gpu_irq_queue_t *queue,
                              const gpu_irq_event_t *event);
gpu_irq_status_t gpu_irq_pop(gpu_irq_queue_t *queue,
                             gpu_irq_event_t *event);
gpu_irq_status_t gpu_irq_drain(gpu_irq_queue_t *queue, size_t max_events,
                               gpu_irq_dispatch_fn dispatch, void *context,
                               size_t *out_processed);
gpu_irq_status_t gpu_irq_mark_device_lost(gpu_irq_queue_t *queue);
/* Call reset only after both producer and consumer are quiesced. */
gpu_irq_status_t gpu_irq_reset(gpu_irq_queue_t *queue);
int gpu_irq_queue_invariant(const gpu_irq_queue_t *queue);

#endif
