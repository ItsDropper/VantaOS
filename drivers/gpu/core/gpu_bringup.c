/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_bringup.h"

#include <string.h>

gpu_bringup_status_t gpu_bringup_execute(const gpu_bringup_stage_t *stages,
                                         size_t stage_count, void *context,
                                         gpu_bringup_result_t *result)
{
    gpu_bringup_result_t local = {
        .failed_stage = (size_t)-1,
        .completed_stages = 0U,
        .verified_capabilities = 0U,
        .rollback_complete = 1U
    };

    if (result != NULL)
        *result = local;
    if (stages == NULL || result == NULL || stage_count == 0U ||
        stage_count > GPU_BRINGUP_MAX_STAGES)
        return GPU_BRINGUP_INVALID;

    for (size_t i = 0U; i < stage_count; ++i) {
        const gpu_bringup_stage_t *stage = &stages[i];
        if (stage->name == NULL || stage->prepare == NULL ||
            stage->verify == NULL || stage->rollback == NULL) {
            local.failed_stage = i;
            *result = local;
            return GPU_BRINGUP_INVALID;
        }

        if (!stage->prepare(context)) {
            local.failed_stage = i;
            goto rollback;
        }
        if (!stage->verify(context)) {
            local.failed_stage = i;
            /* A prepared-but-unverified stage must also be undone. */
            if (!stage->rollback(context))
                local.rollback_complete = 0U;
            goto rollback;
        }

        ++local.completed_stages;
        local.verified_capabilities |= stage->capabilities;
    }

    *result = local;
    return GPU_BRINGUP_OK;

rollback:
    while (local.completed_stages != 0U) {
        size_t index = --local.completed_stages;
        if (!stages[index].rollback(context))
            local.rollback_complete = 0U;
    }
    local.verified_capabilities = 0U;
    *result = local;
    return local.rollback_complete ? GPU_BRINGUP_STAGE_FAILED
                                   : GPU_BRINGUP_ROLLBACK_FAILED;
}
