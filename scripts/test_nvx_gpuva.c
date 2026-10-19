/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gpuva.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define VA_START 0x10000000ULL
#define VA_SIZE  (16ULL * 1024ULL * 1024ULL)

static nvx_gpuva_space_t space;

static void check_stats(uint64_t expected_free, size_t expected_ranges,
                        size_t expected_allocs)
{
    uint64_t free_bytes = 0;
    size_t ranges = 0;
    size_t allocations = 0;
    assert(nvx_gpuva_get_stats(&space, &free_bytes, &ranges, &allocations) ==
           NVX_GPUVA_OK);
    assert(free_bytes == expected_free);
    assert(ranges == expected_ranges);
    assert(allocations == expected_allocs);
    assert(nvx_gpuva_validate(&space) == NVX_GPUVA_OK);
}

static void test_basic_split_merge(void)
{
    nvx_gpuva_allocation_t a = {0}, b = {0}, c = {0};
    memset(&space, 0, sizeof(space));
    assert(nvx_gpuva_init(&space, VA_START, VA_SIZE) == NVX_GPUVA_OK);
    check_stats(VA_SIZE, 1U, 0U);
    assert(nvx_gpuva_alloc(&space, 0x2000U, 0x1000U, 0U, &a) == NVX_GPUVA_OK);
    assert(a.base == VA_START && a.size == 0x2000U);
    assert(a.reserved_base == a.base && a.reserved_size == a.size);
    assert(nvx_gpuva_alloc(&space, 0x3000U, 0x4000U, 0U, &b) == NVX_GPUVA_OK);
    assert((b.base & 0x3FFFU) == 0U);
    assert(nvx_gpuva_alloc(&space, 0x1000U, 0x1000U, 0U, &c) == NVX_GPUVA_OK);
    assert(nvx_gpuva_validate(&space) == NVX_GPUVA_OK);
    assert(nvx_gpuva_free(&space, &b) == NVX_GPUVA_OK);
    assert(nvx_gpuva_free(&space, &a) == NVX_GPUVA_OK);
    assert(nvx_gpuva_free(&space, &c) == NVX_GPUVA_OK);
    assert(nvx_gpuva_free(&space, &c) == NVX_GPUVA_DOUBLE_FREE);
    check_stats(VA_SIZE, 1U, 0U);
}

static void test_guard_pages(void)
{
    nvx_gpuva_allocation_t a = {0};
    memset(&space, 0, sizeof(space));
    assert(nvx_gpuva_init(&space, VA_START, VA_SIZE) == NVX_GPUVA_OK);
    assert(nvx_gpuva_alloc(&space, 1U, NVX_GPUVA_PAGE_SIZE,
                           NVX_GPUVA_GUARD_ONE_PAGE, &a) == NVX_GPUVA_OK);
    assert(a.base - a.reserved_base == NVX_GPUVA_PAGE_SIZE);
    assert(a.reserved_size == 3U * NVX_GPUVA_PAGE_SIZE);
    assert(a.size == NVX_GPUVA_PAGE_SIZE);
    assert(nvx_gpuva_free(&space, &a) == NVX_GPUVA_OK);
    check_stats(VA_SIZE, 1U, 0U);
}

static void test_invalid_and_overflow_inputs(void)
{
    nvx_gpuva_allocation_t a = {0};
    memset(&space, 0, sizeof(space));
    assert(nvx_gpuva_init(&space, VA_START, VA_SIZE) == NVX_GPUVA_OK);
    assert(nvx_gpuva_alloc(&space, 0U, 0x1000U, 0U, &a) ==
           NVX_GPUVA_INVALID_ARGUMENT);
    assert(nvx_gpuva_alloc(&space, 1U, 3U, 0U, &a) ==
           NVX_GPUVA_INVALID_ARGUMENT);
    assert(nvx_gpuva_alloc(&space, 1U, 0x1000U, 2U, &a) ==
           NVX_GPUVA_INVALID_ARGUMENT);
    assert(nvx_gpuva_alloc(&space, UINT64_MAX, 0x1000U, 0U, &a) ==
           NVX_GPUVA_INVALID_RANGE);
    assert(nvx_gpuva_free(&space, &a) == NVX_GPUVA_DOUBLE_FREE);
    assert(nvx_gpuva_init(&space, UINT64_MAX - 0xFFFU, 0x2000U) ==
           NVX_GPUVA_INVALID_RANGE);
    assert(nvx_gpuva_init(&space, VA_START + 1U, VA_SIZE) ==
           NVX_GPUVA_INVALID_RANGE);
}

static void test_out_of_space_and_handle_tampering(void)
{
    nvx_gpuva_allocation_t a = {0}, b = {0}, forged;
    memset(&space, 0, sizeof(space));
    assert(nvx_gpuva_init(&space, VA_START, 0x4000U) == NVX_GPUVA_OK);
    assert(nvx_gpuva_alloc(&space, 0x4000U, 0x1000U, 0U, &a) == NVX_GPUVA_OK);
    assert(nvx_gpuva_alloc(&space, 0x1000U, 0x1000U, 0U, &b) ==
           NVX_GPUVA_OUT_OF_SPACE);
    forged = a;
    forged.reserved_base += 0x1000U;
    assert(nvx_gpuva_free(&space, &forged) == NVX_GPUVA_INVALID_HANDLE);
    assert(nvx_gpuva_validate(&space) == NVX_GPUVA_OK);
    assert(nvx_gpuva_free(&space, &a) == NVX_GPUVA_OK);
    check_stats(0x4000U, 1U, 0U);
}

static void test_fragmentation_and_recovery(void)
{
    nvx_gpuva_allocation_t blocks[32] = {{0}};
    memset(&space, 0, sizeof(space));
    assert(nvx_gpuva_init(&space, VA_START, VA_SIZE) == NVX_GPUVA_OK);
    for (size_t i = 0; i < 32U; ++i)
        assert(nvx_gpuva_alloc(&space, NVX_GPUVA_PAGE_SIZE,
                               NVX_GPUVA_PAGE_SIZE, 0U, &blocks[i]) ==
               NVX_GPUVA_OK);
    for (size_t i = 0; i < 32U; i += 2U)
        assert(nvx_gpuva_free(&space, &blocks[i]) == NVX_GPUVA_OK);
    check_stats(VA_SIZE - 16U * NVX_GPUVA_PAGE_SIZE, 17U, 16U);
    for (size_t i = 1; i < 32U; i += 2U)
        assert(nvx_gpuva_free(&space, &blocks[i]) == NVX_GPUVA_OK);
    check_stats(VA_SIZE, 1U, 0U);
}

static void test_randomized_deterministic_sequence(void)
{
    nvx_gpuva_allocation_t handles[128] = {{0}};
    uint32_t seed = 0xC001D00DU;
    memset(&space, 0, sizeof(space));
    assert(nvx_gpuva_init(&space, VA_START, VA_SIZE) == NVX_GPUVA_OK);
    for (unsigned step = 0; step < 2500U; ++step) {
        seed = seed * 1664525U + 1013904223U;
        size_t slot = (seed >> 8) % 128U;
        if (handles[slot].valid) {
            assert(nvx_gpuva_free(&space, &handles[slot]) == NVX_GPUVA_OK);
        } else {
            uint64_t pages = ((seed >> 16) % 8U) + 1U;
            uint64_t alignment = 1ULL << (12U + ((seed >> 28) % 5U));
            nvx_gpuva_status_t status = nvx_gpuva_alloc(
                &space, pages * NVX_GPUVA_PAGE_SIZE, alignment,
                (seed & 0x20U) ? 1U : 0U, &handles[slot]);
            assert(status == NVX_GPUVA_OK || status == NVX_GPUVA_OUT_OF_SPACE ||
                   status == NVX_GPUVA_TABLE_FULL);
        }
        assert(nvx_gpuva_validate(&space) == NVX_GPUVA_OK);
    }
    for (size_t i = 0; i < 128U; ++i)
        if (handles[i].valid)
            assert(nvx_gpuva_free(&space, &handles[i]) == NVX_GPUVA_OK);
    check_stats(VA_SIZE, 1U, 0U);
}

int main(void)
{
    memset(&space, 0, sizeof(space));
    assert(nvx_gpuva_init(0, VA_START, VA_SIZE) == NVX_GPUVA_INVALID_ARGUMENT);
    assert(nvx_gpuva_get_stats(&space, &(uint64_t){0}, &(size_t){0},
                               &(size_t){0}) == NVX_GPUVA_NOT_INITIALIZED);
    test_basic_split_merge();
    test_guard_pages();
    test_invalid_and_overflow_inputs();
    test_out_of_space_and_handle_tampering();
    test_fragmentation_and_recovery();
    test_randomized_deterministic_sequence();
    return 0;
}
