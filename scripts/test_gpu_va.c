/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_va.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define VA_START 0x10000000ULL
#define VA_SIZE  (16ULL * 1024ULL * 1024ULL)

static gpu_va_space_t space;

static void check_stats(uint64_t expected_free, size_t expected_ranges,
                        size_t expected_allocs)
{
    uint64_t free_bytes = 0;
    size_t ranges = 0;
    size_t allocations = 0;
    assert(gpu_va_get_stats(&space, &free_bytes, &ranges, &allocations) ==
           GPU_VA_OK);
    assert(free_bytes == expected_free);
    assert(ranges == expected_ranges);
    assert(allocations == expected_allocs);
    assert(gpu_va_validate(&space) == GPU_VA_OK);
}

static void test_basic_split_merge(void)
{
    gpu_va_allocation_t a = {0}, b = {0}, c = {0};
    memset(&space, 0, sizeof(space));
    assert(gpu_va_init(&space, VA_START, VA_SIZE) == GPU_VA_OK);
    check_stats(VA_SIZE, 1U, 0U);
    assert(gpu_va_alloc(&space, 0x2000U, 0x1000U, 0U, &a) == GPU_VA_OK);
    assert(a.base == VA_START && a.size == 0x2000U);
    assert(a.reserved_base == a.base && a.reserved_size == a.size);
    assert(gpu_va_alloc(&space, 0x3000U, 0x4000U, 0U, &b) == GPU_VA_OK);
    assert((b.base & 0x3FFFU) == 0U);
    assert(gpu_va_alloc(&space, 0x1000U, 0x1000U, 0U, &c) == GPU_VA_OK);
    assert(gpu_va_validate(&space) == GPU_VA_OK);
    assert(gpu_va_free(&space, &b) == GPU_VA_OK);
    assert(gpu_va_free(&space, &a) == GPU_VA_OK);
    assert(gpu_va_free(&space, &c) == GPU_VA_OK);
    assert(gpu_va_free(&space, &c) == GPU_VA_DOUBLE_FREE);
    check_stats(VA_SIZE, 1U, 0U);
}

static void test_guard_pages(void)
{
    gpu_va_allocation_t a = {0};
    memset(&space, 0, sizeof(space));
    assert(gpu_va_init(&space, VA_START, VA_SIZE) == GPU_VA_OK);
    assert(gpu_va_alloc(&space, 1U, GPU_VA_PAGE_SIZE,
                           GPU_VA_GUARD_ONE_PAGE, &a) == GPU_VA_OK);
    assert(a.base - a.reserved_base == GPU_VA_PAGE_SIZE);
    assert(a.reserved_size == 3U * GPU_VA_PAGE_SIZE);
    assert(a.size == GPU_VA_PAGE_SIZE);
    assert(gpu_va_free(&space, &a) == GPU_VA_OK);
    check_stats(VA_SIZE, 1U, 0U);
}

static void test_invalid_and_overflow_inputs(void)
{
    gpu_va_allocation_t a = {0};
    memset(&space, 0, sizeof(space));
    assert(gpu_va_init(&space, VA_START, VA_SIZE) == GPU_VA_OK);
    assert(gpu_va_alloc(&space, 0U, 0x1000U, 0U, &a) ==
           GPU_VA_INVALID_ARGUMENT);
    assert(gpu_va_alloc(&space, 1U, 3U, 0U, &a) ==
           GPU_VA_INVALID_ARGUMENT);
    assert(gpu_va_alloc(&space, 1U, 0x1000U, 2U, &a) ==
           GPU_VA_INVALID_ARGUMENT);
    assert(gpu_va_alloc(&space, UINT64_MAX, 0x1000U, 0U, &a) ==
           GPU_VA_INVALID_RANGE);
    assert(gpu_va_free(&space, &a) == GPU_VA_DOUBLE_FREE);
    assert(gpu_va_init(&space, UINT64_MAX - 0xFFFU, 0x2000U) ==
           GPU_VA_INVALID_RANGE);
    assert(gpu_va_init(&space, VA_START + 1U, VA_SIZE) ==
           GPU_VA_INVALID_RANGE);
}

static void test_out_of_space_and_handle_tampering(void)
{
    gpu_va_allocation_t a = {0}, b = {0}, forged;
    memset(&space, 0, sizeof(space));
    assert(gpu_va_init(&space, VA_START, 0x4000U) == GPU_VA_OK);
    assert(gpu_va_alloc(&space, 0x4000U, 0x1000U, 0U, &a) == GPU_VA_OK);
    assert(gpu_va_alloc(&space, 0x1000U, 0x1000U, 0U, &b) ==
           GPU_VA_OUT_OF_SPACE);
    forged = a;
    forged.reserved_base += 0x1000U;
    assert(gpu_va_free(&space, &forged) == GPU_VA_INVALID_HANDLE);
    assert(gpu_va_validate(&space) == GPU_VA_OK);
    assert(gpu_va_free(&space, &a) == GPU_VA_OK);
    check_stats(0x4000U, 1U, 0U);
}

static void test_fragmentation_and_recovery(void)
{
    gpu_va_allocation_t blocks[32] = {{0}};
    memset(&space, 0, sizeof(space));
    assert(gpu_va_init(&space, VA_START, VA_SIZE) == GPU_VA_OK);
    for (size_t i = 0; i < 32U; ++i)
        assert(gpu_va_alloc(&space, GPU_VA_PAGE_SIZE,
                               GPU_VA_PAGE_SIZE, 0U, &blocks[i]) ==
               GPU_VA_OK);
    for (size_t i = 0; i < 32U; i += 2U)
        assert(gpu_va_free(&space, &blocks[i]) == GPU_VA_OK);
    check_stats(VA_SIZE - 16U * GPU_VA_PAGE_SIZE, 17U, 16U);
    for (size_t i = 1; i < 32U; i += 2U)
        assert(gpu_va_free(&space, &blocks[i]) == GPU_VA_OK);
    check_stats(VA_SIZE, 1U, 0U);
}

static void test_full_record_table_and_worst_case_fragmentation(void)
{
    gpu_va_allocation_t handles[GPU_VA_MAX_ALLOCS] = {{0}};
    gpu_va_allocation_t overflow = {0};
    memset(&space, 0, sizeof(space));
    assert(gpu_va_init(&space, VA_START, VA_SIZE) == GPU_VA_OK);

    for (size_t i = 0; i < GPU_VA_MAX_ALLOCS; ++i)
        assert(gpu_va_alloc(&space, GPU_VA_PAGE_SIZE,
                               GPU_VA_PAGE_SIZE, 0U, &handles[i]) ==
               GPU_VA_OK);
    assert(gpu_va_alloc(&space, GPU_VA_PAGE_SIZE,
                           GPU_VA_PAGE_SIZE, 0U, &overflow) ==
           GPU_VA_TABLE_FULL);
    assert(gpu_va_validate(&space) == GPU_VA_OK);

    for (size_t i = 0; i < GPU_VA_MAX_ALLOCS; i += 2U)
        assert(gpu_va_free(&space, &handles[i]) == GPU_VA_OK);
    check_stats(VA_SIZE - (GPU_VA_MAX_ALLOCS / 2U) *
                GPU_VA_PAGE_SIZE, (GPU_VA_MAX_ALLOCS / 2U) + 1U,
                GPU_VA_MAX_ALLOCS / 2U);
    for (size_t i = 1; i < GPU_VA_MAX_ALLOCS; i += 2U)
        assert(gpu_va_free(&space, &handles[i]) == GPU_VA_OK);
    check_stats(VA_SIZE, 1U, 0U);
}

static void test_randomized_deterministic_sequence(void)
{
    gpu_va_allocation_t handles[128] = {{0}};
    uint32_t seed = 0xC001D00DU;
    memset(&space, 0, sizeof(space));
    assert(gpu_va_init(&space, VA_START, VA_SIZE) == GPU_VA_OK);
    for (unsigned step = 0; step < 2500U; ++step) {
        seed = seed * 1664525U + 1013904223U;
        size_t slot = (seed >> 8) % 128U;
        if (handles[slot].valid) {
            assert(gpu_va_free(&space, &handles[slot]) == GPU_VA_OK);
        } else {
            uint64_t pages = ((seed >> 16) % 8U) + 1U;
            uint64_t alignment = 1ULL << (12U + ((seed >> 28) % 5U));
            gpu_va_status_t status = gpu_va_alloc(
                &space, pages * GPU_VA_PAGE_SIZE, alignment,
                (seed & 0x20U) ? 1U : 0U, &handles[slot]);
            assert(status == GPU_VA_OK || status == GPU_VA_OUT_OF_SPACE ||
                   status == GPU_VA_TABLE_FULL);
        }
        assert(gpu_va_validate(&space) == GPU_VA_OK);
    }
    for (size_t i = 0; i < 128U; ++i)
        if (handles[i].valid)
            assert(gpu_va_free(&space, &handles[i]) == GPU_VA_OK);
    check_stats(VA_SIZE, 1U, 0U);
}

int main(void)
{
    memset(&space, 0, sizeof(space));
    assert(gpu_va_init(0, VA_START, VA_SIZE) == GPU_VA_INVALID_ARGUMENT);
    assert(gpu_va_get_stats(&space, &(uint64_t){0}, &(size_t){0},
                               &(size_t){0}) == GPU_VA_NOT_INITIALIZED);
    test_basic_split_merge();
    test_guard_pages();
    test_invalid_and_overflow_inputs();
    test_out_of_space_and_handle_tampering();
    test_fragmentation_and_recovery();
    test_full_record_table_and_worst_case_fragmentation();
    test_randomized_deterministic_sequence();
    return 0;
}
