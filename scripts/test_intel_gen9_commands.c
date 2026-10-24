/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9_commands.h"

#include <assert.h>
#include <stdint.h>

int main(void)
{
    uint32_t batch[INTEL_GEN9_FENCE_BATCH_DWORDS] = {0};
    size_t count = 0U;

    assert(intel_gen9_build_store_dword_batch(batch,
        INTEL_GEN9_FENCE_BATCH_DWORDS, 0x12345000U, 0xC0FFEE42U,
        &count) == INTEL_GEN9_COMMAND_OK);
    assert(count == INTEL_GEN9_FENCE_BATCH_DWORDS);
    assert(batch[0] == (INTEL_GEN9_MI_STORE_DWORD_IMM_GEN4 |
                        INTEL_GEN9_MI_USE_GGTT));
    assert(batch[1] == 0x12345000U);
    assert(batch[2] == 0U);
    assert(batch[3] == 0xC0FFEE42U);
    assert(batch[4] == INTEL_GEN9_MI_BATCH_BUFFER_END);
    assert(intel_gen9_validate_store_dword_batch(batch, count,
        0x12345000U, 0xC0FFEE42U) == INTEL_GEN9_COMMAND_OK);

    assert(intel_gen9_build_store_dword_batch(batch,
        INTEL_GEN9_FENCE_BATCH_DWORDS, 0x12345001U, 1U, &count) ==
        INTEL_GEN9_COMMAND_INVALID_ARGUMENT);
    assert(intel_gen9_build_store_dword_batch(batch,
        INTEL_GEN9_FENCE_BATCH_DWORDS, 0x100000000ULL, 1U, &count) ==
        INTEL_GEN9_COMMAND_INVALID_ARGUMENT);
    assert(intel_gen9_validate_store_dword_batch(batch, count,
        0x12345000U, 0xC0FFEE42U) != INTEL_GEN9_COMMAND_OK);

    assert(intel_gen9_build_store_dword_batch(batch,
        INTEL_GEN9_FENCE_BATCH_DWORDS, 0x12345000U, 0xC0FFEE42U,
        &count) == INTEL_GEN9_COMMAND_OK);
    batch[1] += 4U;
    assert(intel_gen9_validate_store_dword_batch(batch, count,
        0x12345000U, 0xC0FFEE42U) == INTEL_GEN9_COMMAND_UNSUPPORTED);
    return 0;
}
