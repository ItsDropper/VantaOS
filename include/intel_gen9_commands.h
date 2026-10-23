/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_INTEL_GEN9_COMMANDS_H
#define VANTA_INTEL_GEN9_COMMANDS_H

#include <stddef.h>
#include <stdint.h>

typedef enum intel_gen9_command_status {
    INTEL_GEN9_COMMAND_OK = 0,
    INTEL_GEN9_COMMAND_INVALID_ARGUMENT,
    INTEL_GEN9_COMMAND_EMPTY,
    INTEL_GEN9_COMMAND_TRUNCATED,
    INTEL_GEN9_COMMAND_UNSUPPORTED,
    INTEL_GEN9_COMMAND_MISSING_END,
    INTEL_GEN9_COMMAND_TRAILING_DATA,
    INTEL_GEN9_COMMAND_TOO_LARGE
} intel_gen9_command_status_t;

typedef struct intel_gen9_command_summary {
    size_t dword_count;
    size_t noop_count;
    size_t end_offset;
    uint8_t has_batch_end;
} intel_gen9_command_summary_t;

/*
 * Conservative Gen9 MI batch preflight. Only MI_NOOP and MI_BATCH_BUFFER_END
 * are accepted; this does not execute commands or authorize hardware submission.
 */
intel_gen9_command_status_t intel_gen9_validate_batch(
    const uint32_t *dwords, size_t count, size_t max_dwords,
    intel_gen9_command_summary_t *summary);

#endif
