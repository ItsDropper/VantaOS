/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_LIFECYCLE_H
#define VANTA_NVX_LIFECYCLE_H

#include <stdint.h>

/*
 * NVX-specific initialization milestones. These describe software progress;
 * they do not imply a capability unless the implementation has verified it.
 */
typedef enum nvx_lifecycle_state {
    NVX_STATE_DISCOVERED = 0,
    NVX_STATE_PROBING,
    NVX_STATE_RESOURCES_READY,
    NVX_STATE_INITIALIZING,
    NVX_STATE_PROBED,
    NVX_STATE_INITIALIZED,
    NVX_STATE_RUNNING,
    NVX_STATE_QUIESCING,
    NVX_STATE_STOPPED,
    NVX_STATE_FAILED
} nvx_lifecycle_state_t;

typedef enum nvx_lifecycle_error {
    NVX_ERROR_NONE = 0,
    NVX_ERROR_INVALID_TRANSITION,
    NVX_ERROR_RESOURCE_VALIDATION,
    NVX_ERROR_FIRMWARE,
    NVX_ERROR_INITIALIZATION,
    NVX_ERROR_RUNTIME
} nvx_lifecycle_error_t;

typedef struct nvx_lifecycle {
    nvx_lifecycle_state_t state;
    nvx_lifecycle_error_t error;
    uint64_t verified_capabilities;
} nvx_lifecycle_t;

void nvx_lifecycle_init(nvx_lifecycle_t *lifecycle);
int nvx_lifecycle_transition(nvx_lifecycle_t *lifecycle,
                             nvx_lifecycle_state_t next_state);
int nvx_lifecycle_fail(nvx_lifecycle_t *lifecycle,
                       nvx_lifecycle_error_t error);
int nvx_lifecycle_set_capabilities(nvx_lifecycle_t *lifecycle,
                                   uint64_t verified_capabilities);

#endif
