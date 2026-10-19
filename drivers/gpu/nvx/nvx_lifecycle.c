/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_lifecycle.h"

static int nvx_transition_allowed(nvx_lifecycle_state_t current,
                                 nvx_lifecycle_state_t next)
{
    switch (current)
    {
        case NVX_STATE_DISCOVERED:
            return next == NVX_STATE_PROBING;
        case NVX_STATE_PROBING:
            return next == NVX_STATE_RESOURCES_READY ||
                   next == NVX_STATE_QUIESCING;
        case NVX_STATE_RESOURCES_READY:
            return next == NVX_STATE_INITIALIZING ||
                   next == NVX_STATE_QUIESCING;
        case NVX_STATE_INITIALIZING:
            return next == NVX_STATE_PROBED ||
                   next == NVX_STATE_INITIALIZED ||
                   next == NVX_STATE_QUIESCING;
        case NVX_STATE_PROBED:
            return next == NVX_STATE_QUIESCING;
        case NVX_STATE_INITIALIZED:
            return next == NVX_STATE_RUNNING || next == NVX_STATE_QUIESCING;
        case NVX_STATE_RUNNING:
            return next == NVX_STATE_QUIESCING;
        case NVX_STATE_QUIESCING:
            return next == NVX_STATE_STOPPED;
        case NVX_STATE_STOPPED:
        case NVX_STATE_FAILED:
        default:
            return 0;
    }
}

void nvx_lifecycle_init(nvx_lifecycle_t *lifecycle)
{
    if (lifecycle == 0)
        return;
    lifecycle->state = NVX_STATE_DISCOVERED;
    lifecycle->error = NVX_ERROR_NONE;
    lifecycle->verified_capabilities = 0;
}

int nvx_lifecycle_transition(nvx_lifecycle_t *lifecycle,
                             nvx_lifecycle_state_t next_state)
{
    if (lifecycle == 0 || !nvx_transition_allowed(lifecycle->state, next_state))
        return 0;
    lifecycle->state = next_state;
    return 1;
}

int nvx_lifecycle_fail(nvx_lifecycle_t *lifecycle,
                       nvx_lifecycle_error_t error)
{
    if (lifecycle == 0 || error == NVX_ERROR_NONE ||
        lifecycle->state == NVX_STATE_STOPPED ||
        lifecycle->state == NVX_STATE_FAILED)
        return 0;
    lifecycle->state = NVX_STATE_FAILED;
    lifecycle->error = error;
    lifecycle->verified_capabilities = 0;
    return 1;
}

int nvx_lifecycle_set_capabilities(nvx_lifecycle_t *lifecycle,
                                   uint64_t verified_capabilities)
{
    if (lifecycle == 0 ||
        (lifecycle->state != NVX_STATE_INITIALIZED &&
         lifecycle->state != NVX_STATE_RUNNING))
        return 0;
    lifecycle->verified_capabilities = verified_capabilities;
    return 1;
}
