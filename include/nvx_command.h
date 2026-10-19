/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_COMMAND_H
#define VANTA_NVX_COMMAND_H
#include <stdint.h>
typedef enum nvx_queue_kind {
    NVX_QUEUE_GRAPHICS = 0, NVX_QUEUE_COPY, NVX_QUEUE_COMPUTE
} nvx_queue_kind_t;
typedef enum nvx_submission_state {
    NVX_SUBMISSION_EMPTY = 0, NVX_SUBMISSION_RECORDING, NVX_SUBMISSION_READY,
    NVX_SUBMISSION_SUBMITTED, NVX_SUBMISSION_COMPLETED, NVX_SUBMISSION_FAILED
} nvx_submission_state_t;
typedef struct nvx_command_queue nvx_command_queue_t;
typedef struct nvx_command_buffer nvx_command_buffer_t;
typedef struct nvx_fence nvx_fence_t;
typedef struct nvx_submission_desc {
    nvx_queue_kind_t queue;
    nvx_command_buffer_t *command_buffer;
    nvx_fence_t *completion_fence;
    uint64_t wait_value, signal_value;
} nvx_submission_desc_t;
/* Never expose unchecked MMIO or arbitrary command streams to user-mode callers. */
#endif
