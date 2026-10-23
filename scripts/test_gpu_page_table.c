/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_page_table.h"
#include "intel_gen9_pte.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define MOCK_PAGES GPU_PAGE_TABLE_MAX_PAGES

typedef struct mock_allocator {
    uint64_t entries[MOCK_PAGES][GPU_PAGE_TABLE_ENTRIES];
    uint8_t in_use[MOCK_PAGES];
    size_t allocations;
    size_t frees;
    size_t flushes;
    int fail_allocation;
    int fail_flush;
} mock_allocator_t;

static int mock_alloc(void *context, gpu_page_table_page_t *out)
{
    mock_allocator_t *mock = context;
    if (mock->fail_allocation)
        return 0;
    for (size_t i = 0; i < MOCK_PAGES; ++i) {
        if (!mock->in_use[i]) {
            mock->in_use[i] = 1U;
            memset(mock->entries[i], 0, sizeof(mock->entries[i]));
            out->entries = mock->entries[i];
            out->physical_address = 0x100000ULL + i * 0x1000ULL;
            out->cookie = (void *)(uintptr_t)(i + 1U);
            ++mock->allocations;
            return 1;
        }
    }
    return 0;
}

static void mock_free(void *context, gpu_page_table_page_t *page)
{
    mock_allocator_t *mock = context;
    size_t index = (size_t)(uintptr_t)page->cookie - 1U;
    assert(index < MOCK_PAGES && mock->in_use[index]);
    mock->in_use[index] = 0U;
    ++mock->frees;
    memset(page, 0, sizeof(*page));
}

static int mock_directory(void *context, uint64_t physical, uint64_t *out)
{
    (void)context;
    if (out == NULL || physical == 0U ||
        (physical & 0xFFFU) != 0U ||
        (physical & ~GPU_PAGE_TABLE_ADDRESS_MASK) != 0U)
        return 0;
    *out = physical | INTEL_GEN9_PTE_PRESENT | INTEL_GEN9_PTE_WRITABLE;
    return 1;
}

static int mock_leaf(void *context, uint64_t physical, unsigned access,
                     uint64_t *out)
{
    (void)context;
    if (access != 1U && access != 3U)
        return 0;
    return intel_gen9_encode_pte(physical, (access & 2U) != 0U,
               INTEL_GEN9_CACHE_WRITE_BACK, out) == INTEL_GEN9_PTE_OK;
}

static int mock_flush(void *context, uint64_t root)
{
    mock_allocator_t *mock = context;
    assert(root != 0U);
    ++mock->flushes;
    return !mock->fail_flush;
}

static void test_mapping_lifecycle(void)
{
    mock_allocator_t mock;
    gpu_page_table_t table;
    gpu_page_table_ops_t ops;
    size_t allocated_before;

    memset(&mock, 0, sizeof(mock));
    memset(&table, 0, sizeof(table));
    ops.context = &mock;
    ops.allocate_page = mock_alloc;
    ops.free_page = mock_free;
    ops.encode_directory = mock_directory;
    ops.encode_leaf = mock_leaf;
    ops.flush = mock_flush;

    assert(gpu_page_table_init(&table, &ops) == GPU_PAGE_TABLE_OK);
    assert(table.root_physical != 0U && mock.allocations == 1U);

    assert(gpu_page_table_map(&table, 0x00001234567000ULL, 0x200000ULL,
                              3U) == GPU_PAGE_TABLE_OK);
    assert(mock.allocations == 4U && mock.flushes == 1U);
    assert(gpu_page_table_map(&table, 0x00001234567000ULL, 0x201000ULL,
                              3U) == GPU_PAGE_TABLE_ALREADY_MAPPED);

    allocated_before = mock.allocations;
    assert(gpu_page_table_unmap(&table, 0x00002234567000ULL) ==
           GPU_PAGE_TABLE_NOT_MAPPED);
    assert(mock.allocations == allocated_before);

    assert(gpu_page_table_map(&table, 0x00001234568000ULL, 0x201000ULL,
                              1U) == GPU_PAGE_TABLE_OK);
    assert(mock.allocations == allocated_before);
    assert(gpu_page_table_unmap(&table, 0x00001234567000ULL) ==
           GPU_PAGE_TABLE_OK);
    assert(gpu_page_table_unmap(&table, 0x00001234567000ULL) ==
           GPU_PAGE_TABLE_NOT_MAPPED);

    assert(gpu_page_table_map(&table, 0x1000U, 0x200001U, 1U) ==
           GPU_PAGE_TABLE_ADDRESS_INVALID);
    assert(gpu_page_table_map(&table, 1ULL << 48U, 0x200000U, 1U) ==
           GPU_PAGE_TABLE_ADDRESS_INVALID);
    assert(gpu_page_table_map(&table, 0x1000U, 0x0001000000000000ULL, 1U) ==
           GPU_PAGE_TABLE_ADDRESS_INVALID);
    assert(gpu_page_table_map(&table, 0x1000U, 0x200000U, 8U) ==
           GPU_PAGE_TABLE_ENCODE_FAILED);

    assert(gpu_page_table_destroy(&table, 0) == GPU_PAGE_TABLE_FAULTED);
    assert(gpu_page_table_destroy(&table, 1) == GPU_PAGE_TABLE_OK);
    assert(mock.frees == mock.allocations);
}

static void test_fail_closed_on_flush_failure(void)
{
    mock_allocator_t mock;
    gpu_page_table_t table;
    gpu_page_table_ops_t ops;

    memset(&mock, 0, sizeof(mock));
    memset(&table, 0, sizeof(table));
    ops.context = &mock;
    ops.allocate_page = mock_alloc;
    ops.free_page = mock_free;
    ops.encode_directory = mock_directory;
    ops.encode_leaf = mock_leaf;
    ops.flush = mock_flush;

    assert(gpu_page_table_init(&table, &ops) == GPU_PAGE_TABLE_OK);
    mock.fail_flush = 1;
    assert(gpu_page_table_map(&table, 0x4000U, 0x300000U, 1U) ==
           GPU_PAGE_TABLE_FLUSH_FAILED);
    assert(table.faulted);
    assert(gpu_page_table_unmap(&table, 0x4000U) == GPU_PAGE_TABLE_FAULTED);
    assert(gpu_page_table_destroy(&table, 0) == GPU_PAGE_TABLE_FAULTED);
    assert(gpu_page_table_destroy(&table, 1) == GPU_PAGE_TABLE_OK);
    assert(mock.frees == mock.allocations);
}

int main(void)
{
    test_mapping_lifecycle();
    test_fail_closed_on_flush_failure();
    return 0;
}
