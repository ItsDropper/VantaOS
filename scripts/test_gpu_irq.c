/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_irq.h"

#include <assert.h>
#include <stdint.h>

typedef struct dispatch_state {
    uint64_t last_sequence;
    uint64_t count;
    int fail;
} dispatch_state_t;

static int dispatch(void *context, const gpu_irq_event_t *event)
{
    dispatch_state_t *state = (dispatch_state_t *)context;
    if (state->fail)
        return 0;
    assert(event->sequence == state->last_sequence);
    ++state->last_sequence;
    ++state->count;
    return 1;
}

int main(void)
{
    gpu_irq_queue_t queue;
    gpu_irq_event_t storage[4], event = {0}, out;
    dispatch_state_t state = {0};
    size_t processed = 0U;

    assert(gpu_irq_queue_init(NULL, storage, 4U) == GPU_IRQ_INVALID_ARGUMENT);
    assert(gpu_irq_queue_init(&queue, storage, 3U) == GPU_IRQ_INVALID_ARGUMENT);
    assert(gpu_irq_queue_init(&queue, storage, 4U) == GPU_IRQ_OK);
    assert(gpu_irq_queue_invariant(&queue));
    assert(gpu_irq_push(&queue, &event) == GPU_IRQ_NOT_READY);
    assert(gpu_irq_queue_enable(&queue) == GPU_IRQ_OK);
    assert(gpu_irq_pop(&queue, &out) == GPU_IRQ_EMPTY);

    event.type = GPU_IRQ_EVENT_COMPLETION;
    event.fence = 1U;
    assert(gpu_irq_push(&queue, &event) == GPU_IRQ_OK);
    event.type = GPU_IRQ_EVENT_VBLANK;
    assert(gpu_irq_push(&queue, &event) == GPU_IRQ_OK);
    event.type = GPU_IRQ_EVENT_FAULT;
    assert(gpu_irq_push(&queue, &event) == GPU_IRQ_OK);
    event.type = GPU_IRQ_EVENT_COMPLETION;
    assert(gpu_irq_push(&queue, &event) == GPU_IRQ_OK);
    assert(gpu_irq_push(&queue, &event) == GPU_IRQ_FULL);
    assert(queue.dropped_events == 1U);
    assert(gpu_irq_queue_invariant(&queue));

    assert(gpu_irq_drain(&queue, 2U, dispatch, &state, &processed) == GPU_IRQ_OK);
    assert(processed == 2U && state.count == 2U);
    assert(gpu_irq_push(&queue, &event) == GPU_IRQ_OK);
    assert(gpu_irq_drain(&queue, 8U, dispatch, &state, &processed) == GPU_IRQ_OK);
    assert(processed == 3U && state.count == 5U);
    assert(gpu_irq_queue_invariant(&queue));

    event.type = GPU_IRQ_EVENT_DEVICE_LOST;
    assert(gpu_irq_push(&queue, &event) == GPU_IRQ_OK);
    assert(queue.device_lost && !queue.enabled);
    assert(gpu_irq_queue_enable(&queue) == GPU_IRQ_DEVICE_LOST);
    assert(gpu_irq_pop(&queue, &out) == GPU_IRQ_OK);
    assert(out.type == GPU_IRQ_EVENT_DEVICE_LOST);
    assert(gpu_irq_reset(&queue) == GPU_IRQ_OK);
    assert(queue.producer_sequence == 0U && queue.consumer_sequence == 0U);
    assert(gpu_irq_queue_enable(&queue) == GPU_IRQ_OK);
    state.fail = 1;
    assert(gpu_irq_push(&queue, &(gpu_irq_event_t){ .type = GPU_IRQ_EVENT_FAULT })
           == GPU_IRQ_OK);
    assert(gpu_irq_drain(&queue, 1U, dispatch, &state, &processed) ==
           GPU_IRQ_DISPATCH_FAILED);
    assert(queue.device_lost && !queue.enabled && processed == 1U);
    assert(gpu_irq_reset(&queue) == GPU_IRQ_OK);
    assert(gpu_irq_mark_device_lost(&queue) == GPU_IRQ_OK);
    assert(gpu_irq_push(&queue, &event) == GPU_IRQ_DEVICE_LOST);
    return 0;
}
