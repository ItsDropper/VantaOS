/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_SYNC_H
#define VANTA_NVX_SYNC_H

#include <stddef.h>
#include <stdint.h>
#include "spinlock.h"

#define NVX_SYNC_MAX_WAITERS 128U

typedef enum nvx_sync_status {
    NVX_SYNC_OK = 0,
    NVX_SYNC_INVALID_ARGUMENT,
    NVX_SYNC_NOT_INITIALIZED,
    NVX_SYNC_VALUE_EXHAUSTED,
    NVX_SYNC_INVALID_SIGNAL,
    NVX_SYNC_DEVICE_LOST,
    NVX_SYNC_STALE_FENCE
} nvx_sync_status_t;

typedef enum nvx_fence_state {
    NVX_FENCE_PENDING = 0,
    NVX_FENCE_SIGNALED,
    NVX_FENCE_FAILED
} nvx_fence_state_t;

typedef struct nvx_fence {
    uint64_t value;
    uint32_t generation;
    uint8_t valid;
} nvx_fence_t;

/*
 * A software monotonic timeline. This is a CPU-side completion contract only;
 * it is not an NVIDIA hardware semaphore or a GPU memory fence.
 */
typedef struct nvx_sync_timeline {
    spinlock_t lock;
    uint64_t next_value;
    uint64_t last_reserved;
    uint64_t completed_value;
    uint32_t generation;
    uint8_t initialized;
    uint8_t device_lost;
} nvx_sync_timeline_t;

nvx_sync_status_t nvx_sync_init(nvx_sync_timeline_t *timeline);
nvx_sync_status_t nvx_sync_reserve(nvx_sync_timeline_t *timeline,
                                   nvx_fence_t *out_fence);
/* Called only after a trusted completion source proves this value completed. */
nvx_sync_status_t nvx_sync_signal(nvx_sync_timeline_t *timeline,
                                  uint64_t completed_value);
nvx_sync_status_t nvx_sync_fail_device(nvx_sync_timeline_t *timeline);
nvx_sync_status_t nvx_sync_query(nvx_sync_timeline_t *timeline,
                                 const nvx_fence_t *fence,
                                 nvx_fence_state_t *out_state);
nvx_sync_status_t nvx_sync_wait_many(nvx_sync_timeline_t *timeline,
                                     const nvx_fence_t *fences,
                                     size_t fence_count,
                                     nvx_fence_state_t *out_states);

#endif
