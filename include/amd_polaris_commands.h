/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_AMD_POLARIS_COMMANDS_H
#define VANTA_AMD_POLARIS_COMMANDS_H

#include <stddef.h>
#include <stdint.h>

typedef enum amd_polaris_pm4_status {
    AMD_POLARIS_PM4_OK = 0,
    AMD_POLARIS_PM4_INVALID_ARGUMENT,
    AMD_POLARIS_PM4_EMPTY,
    AMD_POLARIS_PM4_TOO_LARGE,
    AMD_POLARIS_PM4_TRUNCATED,
    AMD_POLARIS_PM4_UNSUPPORTED_PACKET,
    AMD_POLARIS_PM4_MISSING_TERMINATOR
} amd_polaris_pm4_status_t;

typedef struct amd_polaris_pm4_summary {
    size_t dword_count;
    size_t noop_packet_count;
    size_t type2_packet_count;
    size_t type3_packet_count;
} amd_polaris_pm4_summary_t;

#define AMD_POLARIS_PM4_PACKET3_WRITE_DATA 0xC0033700U
#define AMD_POLARIS_PM4_WRITE_DATA_MEMORY   ((5U << 8) | (1U << 20))
#define AMD_POLARIS_PM4_FENCE_DWORDS        5U

/*
 * Builds/validates a single WRITE_DATA packet that writes exactly one dword
 * to a caller-authorized mapped fence address. It does not authorize arbitrary
 * memory writes or establish that a command processor ring is initialized.
 */
amd_polaris_pm4_status_t amd_polaris_build_fence_packet(
    uint32_t *out_dwords, size_t capacity, uint64_t fence_gpu_address,
    uint32_t fence_value, size_t *out_count);
amd_polaris_pm4_status_t amd_polaris_validate_fence_packet(
    const uint32_t *dwords, size_t count, uint64_t allowed_fence_gpu_address,
    uint32_t expected_value);

/*
 * Conservative GFX8 PM4 framing validator. Only type-2 NOP and type-3 NOP
 * packets are accepted. It deliberately rejects state changes and execution
 * packets until their complete privilege/resource validators exist.
 */
amd_polaris_pm4_status_t amd_polaris_validate_pm4_noop_stream(
    const uint32_t *dwords, size_t count, size_t max_dwords,
    amd_polaris_pm4_summary_t *summary);

#endif
