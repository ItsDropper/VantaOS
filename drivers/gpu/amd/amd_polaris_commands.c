/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_commands.h"

#include <string.h>

#define AMD_PM4_TYPE_MASK 0xC0000000U
#define AMD_PM4_TYPE2 0x80000000U
#define AMD_PM4_TYPE3 0xC0000000U
#define AMD_PM4_TYPE3_OPCODE_MASK 0x0000FF00U
#define AMD_PM4_TYPE3_COUNT_MASK 0x3FFF0000U
#define AMD_PM4_TYPE3_NOP 0x00001000U


amd_polaris_pm4_status_t amd_polaris_build_fence_packet(
    uint32_t *out_dwords, size_t capacity, uint64_t fence_gpu_address,
    uint32_t fence_value, size_t *out_count)
{
    if (out_count == NULL)
        return AMD_POLARIS_PM4_INVALID_ARGUMENT;
    *out_count = 0U;
    if (out_dwords == NULL)
        return AMD_POLARIS_PM4_INVALID_ARGUMENT;
    if (capacity < AMD_POLARIS_PM4_FENCE_DWORDS)
        return AMD_POLARIS_PM4_TOO_LARGE;
    if ((fence_gpu_address & 3U) != 0U ||
        fence_gpu_address >= (1ULL << 48U))
        return AMD_POLARIS_PM4_INVALID_ARGUMENT;

    out_dwords[0] = AMD_POLARIS_PM4_PACKET3_WRITE_DATA;
    out_dwords[1] = AMD_POLARIS_PM4_WRITE_DATA_MEMORY;
    out_dwords[2] = (uint32_t)fence_gpu_address;
    out_dwords[3] = (uint32_t)(fence_gpu_address >> 32U);
    out_dwords[4] = fence_value;
    *out_count = AMD_POLARIS_PM4_FENCE_DWORDS;
    return AMD_POLARIS_PM4_OK;
}

amd_polaris_pm4_status_t amd_polaris_validate_fence_packet(
    const uint32_t *dwords, size_t count, uint64_t allowed_fence_gpu_address,
    uint32_t expected_value)
{
    uint64_t address;

    if (dwords == NULL || count != AMD_POLARIS_PM4_FENCE_DWORDS)
        return AMD_POLARIS_PM4_INVALID_ARGUMENT;
    if ((allowed_fence_gpu_address & 3U) != 0U ||
        allowed_fence_gpu_address >= (1ULL << 48U))
        return AMD_POLARIS_PM4_INVALID_ARGUMENT;
    if (dwords[0] != AMD_POLARIS_PM4_PACKET3_WRITE_DATA ||
        dwords[1] != AMD_POLARIS_PM4_WRITE_DATA_MEMORY)
        return AMD_POLARIS_PM4_UNSUPPORTED_PACKET;

    address = (uint64_t)dwords[2] | ((uint64_t)dwords[3] << 32U);
    if (address != allowed_fence_gpu_address || dwords[4] != expected_value)
        return AMD_POLARIS_PM4_UNSUPPORTED_PACKET;
    return AMD_POLARIS_PM4_OK;
}

amd_polaris_pm4_status_t amd_polaris_validate_pm4_noop_stream(
    const uint32_t *dwords, size_t count, size_t max_dwords,
    amd_polaris_pm4_summary_t *summary)
{
    amd_polaris_pm4_summary_t result;
    size_t i = 0U;

    if (summary == NULL || (dwords == NULL && count != 0U))
        return AMD_POLARIS_PM4_INVALID_ARGUMENT;
    memset(&result, 0, sizeof(result));
    *summary = result;

    if (count == 0U)
        return AMD_POLARIS_PM4_EMPTY;
    if (count > max_dwords)
        return AMD_POLARIS_PM4_TOO_LARGE;

    while (i < count) {
        uint32_t header = dwords[i];
        uint32_t type = header & AMD_PM4_TYPE_MASK;
        if (type == AMD_PM4_TYPE2) {
            if (header != AMD_PM4_TYPE2)
                return AMD_POLARIS_PM4_UNSUPPORTED_PACKET;
            ++result.type2_packet_count;
            ++result.noop_packet_count;
            ++i;
            continue;
        }
        if (type == AMD_PM4_TYPE3) {
            size_t payload_dwords;
            if ((header & AMD_PM4_TYPE3_OPCODE_MASK) != AMD_PM4_TYPE3_NOP)
                return AMD_POLARIS_PM4_UNSUPPORTED_PACKET;
            payload_dwords = (size_t)((header & AMD_PM4_TYPE3_COUNT_MASK) >> 16) + 1U;
            if (payload_dwords > count - i - 1U)
                return AMD_POLARIS_PM4_TRUNCATED;
            ++result.type3_packet_count;
            ++result.noop_packet_count;
            i += payload_dwords + 1U;
            continue;
        }
        return AMD_POLARIS_PM4_UNSUPPORTED_PACKET;
    }

    result.dword_count = count;
    *summary = result;
    return AMD_POLARIS_PM4_OK;
}
