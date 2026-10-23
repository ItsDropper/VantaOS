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


static void test_intel_fence_batch(void)
{
    uint32_t words[INTEL_GEN9_FENCE_BATCH_DWORDS];
    size_t count = 99U;
    const uint64_t target = 0x00001234567000ULL;
    const uint32_t value = 0xA55A1234U;

    assert(intel_gen9_build_fence_batch(words, 5U, target, value, &count) ==
           INTEL_GEN9_COMMAND_OK);
    assert(count == INTEL_GEN9_FENCE_BATCH_DWORDS);
    assert(intel_gen9_validate_fence_batch(words, count, target, value) ==
           INTEL_GEN9_COMMAND_OK);

    words[1] ^= 4U;
    assert(intel_gen9_validate_fence_batch(words, count, target, value) ==
           INTEL_GEN9_COMMAND_UNSUPPORTED);
    words[1] ^= 4U;
    words[3] ^= 1U;
    assert(intel_gen9_validate_fence_batch(words, count, target, value) ==
           INTEL_GEN9_COMMAND_UNSUPPORTED);
    words[3] ^= 1U;
    words[0] |= INTEL_GEN9_MI_USE_GGTT;
    assert(intel_gen9_validate_fence_batch(words, count, target, value) ==
           INTEL_GEN9_COMMAND_UNSUPPORTED);

    assert(intel_gen9_build_fence_batch(words, 4U, target, value, &count) ==
           INTEL_GEN9_COMMAND_TOO_LARGE);
    assert(count == 0U);
    assert(intel_gen9_build_fence_batch(words, 5U, target + 1U, value, &count) ==
           INTEL_GEN9_COMMAND_INVALID_ARGUMENT);
    assert(intel_gen9_build_fence_batch(words, 5U, 1ULL << 48U, value, &count) ==
           INTEL_GEN9_COMMAND_INVALID_ARGUMENT);
    assert(intel_gen9_build_fence_batch(words, 5U, target, value, NULL) ==
           INTEL_GEN9_COMMAND_INVALID_ARGUMENT);
}

static void test_amd_pm4_noop_stream(void)
{
    amd_polaris_pm4_summary_t s;
    const uint32_t valid[] = {0x80000000U, 0xC0001000U, 0x12345678U};
    const uint32_t truncated[] = {0xC0011000U, 0xDEADBEEFU};
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


static void test_amd_fence_packet(void)
{
    uint32_t words[AMD_POLARIS_PM4_FENCE_DWORDS];
    size_t count = 77U;
    const uint64_t target = 0x0000456789A000ULL;
    const uint32_t value = 0xD00DFEEDU;

    assert(amd_polaris_build_fence_packet(words, 5U, target, value, &count) ==
           AMD_POLARIS_PM4_OK);
    assert(count == AMD_POLARIS_PM4_FENCE_DWORDS);
    assert(amd_polaris_validate_fence_packet(words, count, target, value) ==
           AMD_POLARIS_PM4_OK);

    words[2] ^= 4U;
    assert(amd_polaris_validate_fence_packet(words, count, target, value) ==
           AMD_POLARIS_PM4_UNSUPPORTED_PACKET);
    words[2] ^= 4U;
    words[4] ^= 1U;
    assert(amd_polaris_validate_fence_packet(words, count, target, value) ==
           AMD_POLARIS_PM4_UNSUPPORTED_PACKET);
    words[4] ^= 1U;
    words[1] ^= 1U;
    assert(amd_polaris_validate_fence_packet(words, count, target, value) ==
           AMD_POLARIS_PM4_UNSUPPORTED_PACKET);

    assert(amd_polaris_build_fence_packet(words, 4U, target, value, &count) ==
           AMD_POLARIS_PM4_TOO_LARGE);
    assert(count == 0U);
    assert(amd_polaris_build_fence_packet(words, 5U, target + 1U, value,
                                           &count) ==
           AMD_POLARIS_PM4_INVALID_ARGUMENT);
    assert(amd_polaris_build_fence_packet(words, 5U, 1ULL << 48U, value,
                                           &count) ==
           AMD_POLARIS_PM4_INVALID_ARGUMENT);
    assert(amd_polaris_build_fence_packet(words, 5U, target, value, NULL) ==
           AMD_POLARIS_PM4_INVALID_ARGUMENT);
}

int main(void)
{
    test_intel_batch();
    test_intel_fence_batch();
    test_amd_fence_packet();
    test_amd_pm4_noop_stream();
    return 0;
}
