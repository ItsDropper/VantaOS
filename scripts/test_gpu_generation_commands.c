/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_commands.h"
#include "intel_gen9_commands.h"

#include <assert.h>
#include <stdint.h>
#include <stddef.h>

static void test_intel_batch(void)
{
    intel_gen9_command_summary_t s;
    const uint32_t valid[] = {0U, 0U, 0x05000000U};
    const uint32_t missing_end[] = {0U, 0U};
    const uint32_t trailing[] = {0x05000000U, 0U};
    const uint32_t privileged[] = {0x11000000U};

    assert(intel_gen9_validate_batch(valid, 3U, 8U, &s) == INTEL_GEN9_COMMAND_OK);
    assert(s.dword_count == 3U && s.noop_count == 2U);
    assert(s.has_batch_end && s.end_offset == 2U);
    assert(intel_gen9_validate_batch(missing_end, 2U, 8U, &s) ==
           INTEL_GEN9_COMMAND_MISSING_END);
    assert(intel_gen9_validate_batch(trailing, 2U, 8U, &s) ==
           INTEL_GEN9_COMMAND_TRAILING_DATA);
    assert(intel_gen9_validate_batch(privileged, 1U, 8U, &s) ==
           INTEL_GEN9_COMMAND_UNSUPPORTED);
    assert(intel_gen9_validate_batch(valid, 3U, 2U, &s) ==
           INTEL_GEN9_COMMAND_TOO_LARGE);
    assert(intel_gen9_validate_batch(NULL, 1U, 8U, &s) ==
           INTEL_GEN9_COMMAND_INVALID_ARGUMENT);
    assert(intel_gen9_validate_batch(valid, 0U, 8U, &s) ==
           INTEL_GEN9_COMMAND_EMPTY);
}

static void test_amd_pm4_noop_stream(void)
{
    amd_polaris_pm4_summary_t s;
    const uint32_t valid[] = {0x80000000U, 0xC0001000U, 0x12345678U};
    const uint32_t truncated[] = {0xC0001001U, 0xDEADBEEFU};
    const uint32_t execution[] = {0xC0002D00U};
    const uint32_t malformed_type2[] = {0x80000001U};

    assert(amd_polaris_validate_pm4_noop_stream(valid, 3U, 8U, &s) ==
           AMD_POLARIS_PM4_OK);
    assert(s.dword_count == 3U && s.noop_packet_count == 2U);
    assert(s.type2_packet_count == 1U && s.type3_packet_count == 1U);
    assert(amd_polaris_validate_pm4_noop_stream(truncated, 2U, 8U, &s) ==
           AMD_POLARIS_PM4_TRUNCATED);
    assert(amd_polaris_validate_pm4_noop_stream(execution, 1U, 8U, &s) ==
           AMD_POLARIS_PM4_UNSUPPORTED_PACKET);
    assert(amd_polaris_validate_pm4_noop_stream(malformed_type2, 1U, 8U, &s) ==
           AMD_POLARIS_PM4_UNSUPPORTED_PACKET);
    assert(amd_polaris_validate_pm4_noop_stream(valid, 3U, 2U, &s) ==
           AMD_POLARIS_PM4_TOO_LARGE);
    assert(amd_polaris_validate_pm4_noop_stream(NULL, 1U, 8U, &s) ==
           AMD_POLARIS_PM4_INVALID_ARGUMENT);
    assert(amd_polaris_validate_pm4_noop_stream(valid, 0U, 8U, &s) ==
           AMD_POLARIS_PM4_EMPTY);
}

int main(void)
{
    test_intel_batch();
    test_amd_pm4_noop_stream();
    return 0;
}
