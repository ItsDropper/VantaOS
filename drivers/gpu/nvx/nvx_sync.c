/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_sync.h"

#include <stddef.h>
#include <stdint.h>

nvx_sync_status_t nvx_sync_init(nvx_sync_timeline_t *timeline)
{
    if (timeline == 0)
        return NVX_SYNC_INVALID_ARGUMENT;
    if (timeline->initialized)
        return NVX_SYNC_INVALID_ARGUMENT;

    spinlock_init(&timeline->lock);
    timeline->next_value = 1U;
    timeline->last_reserved = 0U;
    timeline->completed_value = 0U;
    ++timeline->generation;
    if (timeline->generation == 0U)
        timeline->generation = 1U;
    timeline->device_lost = 0U;
    timeline->initialized = 1U;
    return NVX_SYNC_OK;
}

nvx_sync_status_t nvx_sync_reserve(nvx_sync_timeline_t *timeline,
                                   nvx_fence_t *out_fence)
{
    if (out_fence != 0) {
        out_fence->value = 0U;
        out_fence->generation = 0U;
        out_fence->valid = 0U;
    }
    if (timeline == 0 || out_fence == 0)
        return NVX_SYNC_INVALID_ARGUMENT;
    if (!timeline->initialized)
        return NVX_SYNC_NOT_INITIALIZED;

    spinlock_lock(&timeline->lock);
    if (timeline->device_lost) {
        spinlock_unlock(&timeline->lock);
        return NVX_SYNC_DEVICE_LOST;
    }
    if (timeline->next_value == 0U ||
        timeline->next_value == UINT64_MAX) {
        spinlock_unlock(&timeline->lock);
        return NVX_SYNC_VALUE_EXHAUSTED;
    }

    out_fence->value = timeline->next_value++;
    out_fence->generation = timeline->generation;
    out_fence->valid = 1U;
    timeline->last_reserved = out_fence->value;
    spinlock_unlock(&timeline->lock);
    return NVX_SYNC_OK;
}

nvx_sync_status_t nvx_sync_signal(nvx_sync_timeline_t *timeline,
                                  uint64_t completed_value)
{
    if (timeline == 0)
        return NVX_SYNC_INVALID_ARGUMENT;
    if (!timeline->initialized)
        return NVX_SYNC_NOT_INITIALIZED;

    spinlock_lock(&timeline->lock);
    if (timeline->device_lost) {
        spinlock_unlock(&timeline->lock);
        return NVX_SYNC_DEVICE_LOST;
    }
    if (completed_value <= timeline->completed_value ||
        completed_value > timeline->last_reserved) {
        spinlock_unlock(&timeline->lock);
        return NVX_SYNC_INVALID_SIGNAL;
    }
    timeline->completed_value = completed_value;
    spinlock_unlock(&timeline->lock);
    return NVX_SYNC_OK;
}

nvx_sync_status_t nvx_sync_fail_device(nvx_sync_timeline_t *timeline)
{
    if (timeline == 0)
        return NVX_SYNC_INVALID_ARGUMENT;
    if (!timeline->initialized)
        return NVX_SYNC_NOT_INITIALIZED;
    spinlock_lock(&timeline->lock);
    timeline->device_lost = 1U;
    spinlock_unlock(&timeline->lock);
    return NVX_SYNC_OK;
}

nvx_sync_status_t nvx_sync_query(nvx_sync_timeline_t *timeline,
                                 const nvx_fence_t *fence,
                                 nvx_fence_state_t *out_state)
{
    if (out_state != 0)
        *out_state = NVX_FENCE_FAILED;
    if (timeline == 0 || fence == 0 || out_state == 0)
        return NVX_SYNC_INVALID_ARGUMENT;
    if (!timeline->initialized)
        return NVX_SYNC_NOT_INITIALIZED;
    spinlock_lock(&timeline->lock);
    if (!fence->valid || fence->value == 0U ||
        fence->generation != timeline->generation ||
        fence->value > timeline->last_reserved) {
        spinlock_unlock(&timeline->lock);
        return NVX_SYNC_STALE_FENCE;
    }
    if (timeline->device_lost)
        *out_state = NVX_FENCE_FAILED;
    else if (fence->value <= timeline->completed_value)
        *out_state = NVX_FENCE_SIGNALED;
    else
        *out_state = NVX_FENCE_PENDING;
    spinlock_unlock(&timeline->lock);
    return NVX_SYNC_OK;
}

nvx_sync_status_t nvx_sync_wait_many(nvx_sync_timeline_t *timeline,
                                     const nvx_fence_t *fences,
                                     size_t fence_count,
                                     nvx_fence_state_t *out_states)
{
    if (timeline == 0 || (fence_count != 0U &&
        (fences == 0 || out_states == 0)))
        return NVX_SYNC_INVALID_ARGUMENT;
    if (fence_count > NVX_SYNC_MAX_WAITERS)
        return NVX_SYNC_INVALID_ARGUMENT;
    if (!timeline->initialized)
        return NVX_SYNC_NOT_INITIALIZED;

    /* Polling snapshot only: this function never sleeps or spins. */
    spinlock_lock(&timeline->lock);
    for (size_t i = 0; i < fence_count; ++i) {
        const nvx_fence_t *fence = &fences[i];
        if (!fence->valid || fence->value == 0U ||
            fence->generation != timeline->generation ||
            fence->value > timeline->last_reserved) {
            for (size_t j = 0; j < fence_count; ++j)
                out_states[j] = NVX_FENCE_FAILED;
            spinlock_unlock(&timeline->lock);
            return NVX_SYNC_STALE_FENCE;
        }
        if (timeline->device_lost)
            out_states[i] = NVX_FENCE_FAILED;
        else if (fence->value <= timeline->completed_value)
            out_states[i] = NVX_FENCE_SIGNALED;
        else
            out_states[i] = NVX_FENCE_PENDING;
    }
    spinlock_unlock(&timeline->lock);
    return NVX_SYNC_OK;
}
