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

#define INTEL_GEN9_MI_STORE_DWORD_IMM_GEN4 0x10000002U
#define INTEL_GEN9_MI_USE_GGTT             (1U << 22)
#define INTEL_GEN9_MI_BATCH_BUFFER_END     0x05000000U
#define INTEL_GEN9_FENCE_BATCH_DWORDS      5U

/*
 * Builds and validates the narrowly-scoped memory fence batch: one 32-bit
 * store to the caller-provided mapped fence VA followed by BATCH_BUFFER_END.
 * This is not a general arbitrary-write command API.
 */
intel_gen9_command_status_t intel_gen9_build_fence_batch(
    uint32_t *out_dwords, size_t capacity, uint64_t fence_gpu_address,
    uint32_t fence_value, size_t *out_count);
intel_gen9_command_status_t intel_gen9_validate_fence_batch(
    const uint32_t *dwords, size_t count, uint64_t allowed_fence_gpu_address,
    uint32_t expected_value);

/*
 * Conservative Gen9 MI batch preflight. Only MI_NOOP and MI_BATCH_BUFFER_END
 * are accepted; this does not execute commands or authorize hardware submission.
 */
intel_gen9_command_status_t intel_gen9_validate_batch(
    const uint32_t *dwords, size_t count, size_t max_dwords,
    intel_gen9_command_summary_t *summary);

#endif
