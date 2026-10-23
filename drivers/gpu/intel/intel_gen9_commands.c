/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9_commands.h"

#include <string.h>

/* Gen9 MI command encodings: NOOP is zero; BATCH_BUFFER_END is 0x05000000. */
#define INTEL_GEN9_MI_NOOP 0x00000000U
#define INTEL_GEN9_MI_BATCH_BUFFER_END 0x05000000U


intel_gen9_command_status_t intel_gen9_build_fence_batch(
    uint32_t *out_dwords, size_t capacity, uint64_t fence_gpu_address,
    uint32_t fence_value, size_t *out_count)
{
    if (out_count == NULL)
        return INTEL_GEN9_COMMAND_INVALID_ARGUMENT;
    *out_count = 0U;
    if (out_dwords == NULL)
        return INTEL_GEN9_COMMAND_INVALID_ARGUMENT;
    if (capacity < INTEL_GEN9_FENCE_BATCH_DWORDS)
        return INTEL_GEN9_COMMAND_TOO_LARGE;
    if ((fence_gpu_address & 3U) != 0U ||
        fence_gpu_address >= (1ULL << 48U))
        return INTEL_GEN9_COMMAND_INVALID_ARGUMENT;

    out_dwords[0] = INTEL_GEN9_MI_STORE_DWORD_IMM_GEN4;
    out_dwords[1] = (uint32_t)fence_gpu_address;
    out_dwords[2] = (uint32_t)(fence_gpu_address >> 32U);
    out_dwords[3] = fence_value;
    out_dwords[4] = INTEL_GEN9_MI_BATCH_BUFFER_END;
    *out_count = INTEL_GEN9_FENCE_BATCH_DWORDS;
    return INTEL_GEN9_COMMAND_OK;
}

intel_gen9_command_status_t intel_gen9_validate_fence_batch(
    const uint32_t *dwords, size_t count, uint64_t allowed_fence_gpu_address,
    uint32_t expected_value)
{
    uint64_t address;

    if (dwords == NULL || count != INTEL_GEN9_FENCE_BATCH_DWORDS)
        return INTEL_GEN9_COMMAND_INVALID_ARGUMENT;
    if ((allowed_fence_gpu_address & 3U) != 0U ||
        allowed_fence_gpu_address >= (1ULL << 48U))
        return INTEL_GEN9_COMMAND_INVALID_ARGUMENT;
    if (dwords[0] != INTEL_GEN9_MI_STORE_DWORD_IMM_GEN4 ||
        dwords[4] != INTEL_GEN9_MI_BATCH_BUFFER_END)
        return INTEL_GEN9_COMMAND_UNSUPPORTED;

    address = (uint64_t)dwords[1] | ((uint64_t)dwords[2] << 32U);
    if (address != allowed_fence_gpu_address || dwords[3] != expected_value)
        return INTEL_GEN9_COMMAND_UNSUPPORTED;
    return INTEL_GEN9_COMMAND_OK;
}

intel_gen9_command_status_t intel_gen9_validate_batch(
    const uint32_t *dwords, size_t count, size_t max_dwords,
    intel_gen9_command_summary_t *summary)
{
    intel_gen9_command_summary_t result;
    size_t i;

    if (summary == NULL || (dwords == NULL && count != 0U))
        return INTEL_GEN9_COMMAND_INVALID_ARGUMENT;
    memset(&result, 0, sizeof(result));
    result.end_offset = SIZE_MAX;
    *summary = result;

    if (count == 0U)
        return INTEL_GEN9_COMMAND_EMPTY;
    if (count > max_dwords)
        return INTEL_GEN9_COMMAND_TOO_LARGE;

    for (i = 0U; i < count; ++i) {
        uint32_t command = dwords[i];
        if (command == INTEL_GEN9_MI_NOOP) {
            ++result.noop_count;
            continue;
        }
        if (command == INTEL_GEN9_MI_BATCH_BUFFER_END) {
            if (i + 1U != count)
                return INTEL_GEN9_COMMAND_TRAILING_DATA;
            result.has_batch_end = 1U;
            result.end_offset = i;
            result.dword_count = count;
            *summary = result;
            return INTEL_GEN9_COMMAND_OK;
        }
        /* No other command class is admitted by this bring-up allowlist. */
        return INTEL_GEN9_COMMAND_UNSUPPORTED;
    }
    return INTEL_GEN9_COMMAND_MISSING_END;
}
