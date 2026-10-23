/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9_execlist.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define MMIO_BYTES 0x3000U
static uint32_t registers[MMIO_BYTES / sizeof(uint32_t)];

int main(void)
{
    gpu_mmio_region_t mmio = {
        .base = (volatile uint8_t *)registers,
        .length = sizeof(registers)
    };
    intel_gen9_execlist_t state = {0};
    const uint64_t descriptor = (1ULL << 32U) | 0x1000ULL |
                                INTEL_GEN9_CONTEXT_DESCRIPTOR_VALID |
                                (1ULL << 3U);
    uint64_t invalid;

    memset(registers, 0, sizeof(registers));
    assert(intel_gen9_execlist_init(&state, &mmio, 0, 1) ==
           INTEL_GEN9_EXECLIST_NOT_READY);
    assert(intel_gen9_execlist_init(&state, &mmio, 1, 1) ==
           INTEL_GEN9_EXECLIST_OK);
    assert(intel_gen9_execlist_submit(&state, descriptor, 0) ==
           INTEL_GEN9_EXECLIST_BUSY);

    invalid = descriptor & ~INTEL_GEN9_CONTEXT_DESCRIPTOR_VALID;
    assert(intel_gen9_execlist_submit(&state, invalid, 1) ==
           INTEL_GEN9_EXECLIST_INVALID_DESCRIPTOR);
    invalid = descriptor | (1ULL << 60U);
    assert(intel_gen9_execlist_submit(&state, invalid, 1) ==
           INTEL_GEN9_EXECLIST_INVALID_DESCRIPTOR);

    assert(intel_gen9_execlist_submit(&state, descriptor, 1) ==
           INTEL_GEN9_EXECLIST_OK);
    assert(registers[INTEL_GEN9_EXECLIST_ELSP_OFFSET / 4U] ==
           (uint32_t)descriptor);
    assert(intel_gen9_execlist_submit(&state, descriptor, 1) ==
           INTEL_GEN9_EXECLIST_BUSY);
    assert(intel_gen9_execlist_complete(&state, 2U) ==
           INTEL_GEN9_EXECLIST_STALE_COMPLETION);
    assert(intel_gen9_execlist_complete(&state, 1U) ==
           INTEL_GEN9_EXECLIST_OK);
    assert(intel_gen9_execlist_shutdown(&state, 0) ==
           INTEL_GEN9_EXECLIST_NOT_READY);
    assert(intel_gen9_execlist_shutdown(&state, 1) ==
           INTEL_GEN9_EXECLIST_OK);
    return 0;
}
