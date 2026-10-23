/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9_ggtt.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint64_t fake_aperture[INTEL_GEN9_GTTMMADR_SIZE / sizeof(uint64_t)];

typedef struct forcewake_test_context {
    unsigned acquires;
    unsigned releases;
    int acquire_ok;
    int release_ok;
} forcewake_test_context_t;

static int acquire(void *context)
{
    forcewake_test_context_t *state = context;
    ++state->acquires;
    return state->acquire_ok;
}

static int release(void *context)
{
    forcewake_test_context_t *state = context;
    ++state->releases;
    return state->release_ok;
}

static void test_ggtt_map_unmap(void)
{
    gpu_mmio_region_t mmio = {
        .base = (volatile uint8_t *)(void *)fake_aperture,
        .length = sizeof(fake_aperture)
    };
    uint64_t pte = 0U;
    size_t first_pte = (INTEL_GEN9_GGTT_PTE_BASE + 8U);

    memset(fake_aperture, 0, sizeof(fake_aperture));
    assert(intel_gen9_ggtt_map_page(&mmio, 0x1000U, 0x12345000U) ==
           INTEL_GEN9_GGTT_OK);
    assert(gpu_mmio_read64(&mmio, first_pte, &pte) == GPU_MMIO_OK);
    assert(pte == (0x12345000ULL | INTEL_GEN9_PTE_PRESENT));

    assert(intel_gen9_ggtt_unmap_page(&mmio, 0x1000U) ==
           INTEL_GEN9_GGTT_OK);
    assert(gpu_mmio_read64(&mmio, first_pte, &pte) == GPU_MMIO_OK);
    assert(pte == 0U);

    assert(intel_gen9_ggtt_map_page(&mmio, 0x1001U, 0x12345000U) ==
           INTEL_GEN9_GGTT_UNALIGNED);
    assert(intel_gen9_ggtt_map_page(&mmio, 1ULL << 32U, 0x12345000U) ==
           INTEL_GEN9_GGTT_OUT_OF_RANGE);
    assert(intel_gen9_ggtt_map_page(&mmio, 0U, 0x0001000000000000ULL) ==
           INTEL_GEN9_GGTT_OUT_OF_RANGE);
    assert(intel_gen9_ggtt_map_page(&mmio, 0xFFFFF000ULL, 0x200000U) ==
           INTEL_GEN9_GGTT_OK);
    assert(intel_gen9_ggtt_unmap_page(&mmio, 0xFFFFF000ULL) ==
           INTEL_GEN9_GGTT_OK);

    mmio.length = INTEL_GEN9_GTTMMADR_SIZE - 1U;
    assert(intel_gen9_ggtt_map_page(&mmio, 0U, 0x200000U) ==
           INTEL_GEN9_GGTT_INVALID_ARGUMENT);
}

static void test_invalidate_forcewake_contract(void)
{
    gpu_mmio_region_t mmio = {
        .base = (volatile uint8_t *)(void *)fake_aperture,
        .length = sizeof(fake_aperture)
    };
    forcewake_test_context_t state = {
        .acquires = 0U, .releases = 0U, .acquire_ok = 1, .release_ok = 1
    };
    intel_gen9_ggtt_forcewake_ops_t ops = {
        .context = &state,
        .acquire_render = acquire,
        .release_render = release
    };

    memset(fake_aperture, 0, sizeof(fake_aperture));
    assert(intel_gen9_ggtt_invalidate(&mmio, &ops) == INTEL_GEN9_GGTT_OK);
    assert(state.acquires == 1U && state.releases == 1U);

    state.acquire_ok = 0;
    assert(intel_gen9_ggtt_invalidate(&mmio, &ops) ==
           INTEL_GEN9_GGTT_FORCEWAKE_FAILED);
    assert(state.acquires == 2U && state.releases == 1U);

    state.acquire_ok = 1;
    state.release_ok = 0;
    assert(intel_gen9_ggtt_invalidate(&mmio, &ops) ==
           INTEL_GEN9_GGTT_FORCEWAKE_FAILED);
    assert(state.acquires == 3U && state.releases == 2U);
}

static void test_forcewake_timeout(void)
{
    gpu_mmio_region_t mmio = {
        .base = (volatile uint8_t *)(void *)fake_aperture,
        .length = sizeof(fake_aperture)
    };
    uint32_t *ack = (uint32_t *)(void *)(fake_aperture) +
                    (0xD84U / sizeof(uint32_t));

    *ack = 1U;
    assert(intel_gen9_forcewake_render_acquire(&mmio, 2U));
    *ack = 0U;
    assert(intel_gen9_forcewake_render_release(&mmio, 2U));
    *ack = 0U;
    assert(!intel_gen9_forcewake_render_acquire(&mmio, 2U));
    *ack = 1U;
    assert(!intel_gen9_forcewake_render_release(&mmio, 2U));
}

int main(void)
{
    test_ggtt_map_unmap();
    test_invalidate_forcewake_contract();
    test_forcewake_timeout();
    return 0;
}
