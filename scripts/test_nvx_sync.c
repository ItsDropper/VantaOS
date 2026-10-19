/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_sync.h"

#include <assert.h>
#include <string.h>

static nvx_sync_timeline_t timeline;

int main(void)
{
    nvx_fence_t f1 = {0}, f2 = {0}, f3 = {0}, invalid = {0};
    nvx_fence_t many[3];
    nvx_fence_state_t state;
    nvx_fence_state_t states[3];

    memset(&timeline, 0, sizeof(timeline));
    assert(nvx_sync_reserve(&timeline, &f1) == NVX_SYNC_NOT_INITIALIZED);
    assert(nvx_sync_init(&timeline) == NVX_SYNC_OK);
    assert(nvx_sync_init(&timeline) == NVX_SYNC_INVALID_ARGUMENT);
    assert(nvx_sync_reserve(&timeline, &f1) == NVX_SYNC_OK);
    assert(nvx_sync_reserve(&timeline, &f2) == NVX_SYNC_OK);
    assert(nvx_sync_reserve(&timeline, &f3) == NVX_SYNC_OK);
    assert(f1.value == 1U && f2.value == 2U && f3.value == 3U);

    assert(nvx_sync_query(&timeline, &f2, &state) == NVX_SYNC_OK);
    assert(state == NVX_FENCE_PENDING);
    assert(nvx_sync_signal(&timeline, 0U) == NVX_SYNC_INVALID_SIGNAL);
    assert(nvx_sync_signal(&timeline, 4U) == NVX_SYNC_INVALID_SIGNAL);
    assert(nvx_sync_signal(&timeline, 2U) == NVX_SYNC_OK);
    assert(nvx_sync_signal(&timeline, 2U) == NVX_SYNC_INVALID_SIGNAL);
    assert(nvx_sync_query(&timeline, &f1, &state) == NVX_SYNC_OK);
    assert(state == NVX_FENCE_SIGNALED);
    assert(nvx_sync_query(&timeline, &f3, &state) == NVX_SYNC_OK);
    assert(state == NVX_FENCE_PENDING);

    many[0] = f1; many[1] = f2; many[2] = f3;
    assert(nvx_sync_wait_many(&timeline, many, 3U, states) == NVX_SYNC_OK);
    assert(states[0] == NVX_FENCE_SIGNALED);
    assert(states[1] == NVX_FENCE_SIGNALED);
    assert(states[2] == NVX_FENCE_PENDING);
    assert(nvx_sync_wait_many(&timeline, 0, 0U, 0) == NVX_SYNC_OK);
    assert(nvx_sync_wait_many(&timeline, many, 3U, 0) ==
           NVX_SYNC_INVALID_ARGUMENT);
    assert(nvx_sync_query(&timeline, &invalid, &state) ==
           NVX_SYNC_STALE_FENCE);

    invalid = f3;
    invalid.generation++;
    assert(nvx_sync_query(&timeline, &invalid, &state) ==
           NVX_SYNC_STALE_FENCE);
    assert(nvx_sync_fail_device(&timeline) == NVX_SYNC_OK);
    assert(nvx_sync_query(&timeline, &f3, &state) == NVX_SYNC_OK);
    assert(state == NVX_FENCE_FAILED);
    assert(nvx_sync_reserve(&timeline, &invalid) == NVX_SYNC_DEVICE_LOST);
    assert(nvx_sync_signal(&timeline, 3U) == NVX_SYNC_DEVICE_LOST);
    return 0;
}
