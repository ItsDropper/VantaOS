/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_BRINGUP_H
#define VANTA_GPU_BRINGUP_H

#include <stddef.h>
#include <stdint.h>

#define GPU_BRINGUP_MAX_STAGES 16U

typedef enum gpu_bringup_status {
    GPU_BRINGUP_OK = 0,
    GPU_BRINGUP_INVALID,
    GPU_BRINGUP_STAGE_FAILED,
    GPU_BRINGUP_VERIFY_FAILED,
    GPU_BRINGUP_ROLLBACK_FAILED
} gpu_bringup_status_t;

typedef struct gpu_bringup_stage {
    const char *name;
    int (*prepare)(void *context);
    int (*verify)(void *context);
    int (*rollback)(void *context);
    uint64_t capabilities;
} gpu_bringup_stage_t;

typedef struct gpu_bringup_result {
    size_t failed_stage;
    size_t completed_stages;
    uint64_t verified_capabilities;
    uint8_t rollback_complete;
} gpu_bringup_result_t;

/*
 * Runs stages in order and publishes capabilities only after each stage's
 * verification succeeds. Any failure rolls back completed stages in reverse.
 */
gpu_bringup_status_t gpu_bringup_execute(const gpu_bringup_stage_t *stages,
                                         size_t stage_count, void *context,
                                         gpu_bringup_result_t *result);

#endif
