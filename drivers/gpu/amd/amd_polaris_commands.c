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
