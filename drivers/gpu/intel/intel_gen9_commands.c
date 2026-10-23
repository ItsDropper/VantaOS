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
