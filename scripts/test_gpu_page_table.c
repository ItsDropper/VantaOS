/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_page_table.h"
#include "intel_gen9_pte.h"
#include "intel_gen9_vm.h"
#include "amd_polaris_pte.h"
#include "amd_polaris_vm.h"
#include "gpu_page_table_vm.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define MOCK_PAGES GPU_PAGE_TABLE_MAX_PAGES
#define MOCK_LARGE_TABLE_ENTRIES (1U << 22U)

static uint64_t mock_large_table[MOCK_LARGE_TABLE_ENTRIES];

typedef struct mock_allocator {
    uint64_t entries[MOCK_PAGES][GPU_PAGE_TABLE_ENTRIES];
    uint8_t in_use[MOCK_PAGES];
    size_t allocations;
    size_t frees;
    size_t flushes;
    int fail_allocation;
    int fail_flush;
} mock_allocator_t;

static int mock_alloc(void *context, size_t entry_count,
                      gpu_page_table_page_t *out)
{
    mock_allocator_t *mock = context;
    if (entry_count == 0U || entry_count > MOCK_LARGE_TABLE_ENTRIES)
        return 0;
    if (mock->fail_allocation)
        return 0;
    for (size_t i = 0; i < MOCK_PAGES; ++i) {
        if (!mock->in_use[i]) {
            mock->in_use[i] = 1U;
            memset(mock->entries[i], 0, sizeof(mock->entries[i]));
            out->entries = entry_count > GPU_PAGE_TABLE_ENTRIES ?
                           mock_large_table : mock->entries[i];
            out->physical_address = 0x100000ULL + i * 0x100000ULL;
            out->entry_count = entry_count;
            out->allocation_size = entry_count * sizeof(uint64_t);
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
    ops.allocate_table = mock_alloc;
    ops.free_page = mock_free;
    ops.encode_directory = mock_directory;
    ops.encode_leaf = mock_leaf;
    ops.flush = mock_flush;

    assert(gpu_page_table_init(&table, &ops) == GPU_PAGE_TABLE_OK);
    assert(table.root_physical != 0U && mock.allocations == 1U);

    assert(gpu_page_table_map(&table, 0x00001234567000ULL, 0x200000ULL,
                              3U) == GPU_PAGE_TABLE_OK);
    assert(mock.allocations == 4U && mock.flushes == 0U);
    assert(gpu_page_table_flush(&table) == GPU_PAGE_TABLE_OK);
    assert(mock.flushes == 1U);
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
    assert(gpu_page_table_flush(&table) == GPU_PAGE_TABLE_OK);
    assert(gpu_page_table_flush(&table) == GPU_PAGE_TABLE_OK);

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
    ops.allocate_table = mock_alloc;
    ops.free_page = mock_free;
    ops.encode_directory = mock_directory;
    ops.encode_leaf = mock_leaf;
    ops.flush = mock_flush;

    assert(gpu_page_table_init(&table, &ops) == GPU_PAGE_TABLE_OK);
    assert(gpu_page_table_map(&table, 0x4000U, 0x300000U, 1U) ==
           GPU_PAGE_TABLE_OK);
    mock.fail_flush = 1;
    assert(gpu_page_table_flush(&table) == GPU_PAGE_TABLE_FLUSH_FAILED);
    assert(table.faulted);
    assert(gpu_page_table_unmap(&table, 0x4000U) == GPU_PAGE_TABLE_FAULTED);
    assert(gpu_page_table_destroy(&table, 0) == GPU_PAGE_TABLE_FAULTED);
    assert(gpu_page_table_destroy(&table, 1) == GPU_PAGE_TABLE_OK);
    assert(mock.frees == mock.allocations);
}


static uint64_t mapped_leaf_entry(const gpu_page_table_t *table,
                                  uint64_t gpu_address)
{
    const gpu_page_table_page_t *page = &table->pages[0];
    for (unsigned level = 0U; level + 1U < table->config.level_count; ++level) {
        size_t mask = level == 0U ? table->config.root_entry_count - 1U :
                                   GPU_PAGE_TABLE_ENTRIES - 1U;
        size_t index = (size_t)((gpu_address >>
            table->config.index_shifts[level]) & (uint64_t)mask);
        uint64_t physical = page->entries[index] & GPU_PAGE_TABLE_ADDRESS_MASK;
        page = NULL;
        for (size_t i = 0U; i < table->page_count; ++i)
            if (table->pages[i].physical_address == physical)
                page = &table->pages[i];
        assert(page != NULL);
    }
    return page->entries[(gpu_address >>
        table->config.index_shifts[table->config.level_count - 1U]) &
        (uint64_t)(table->config.leaf_entry_count - 1U)];
}

static void test_vendor_vm_backends(void)
{
    mock_allocator_t mock;
    gpu_page_table_t table;
    gpu_page_table_ops_t ops;
    gpu_vm_ops_t generic_vm_ops;
    intel_gen9_vm_backend_t intel_backend;
    amd_polaris_vm_backend_t amd_backend;
    const uint64_t va = 0x00001234567000ULL;

    memset(&mock, 0, sizeof(mock));
    memset(&table, 0, sizeof(table));
    intel_backend.context = &mock;
    intel_backend.allocate_table = mock_alloc;
    intel_backend.free_page = mock_free;
    intel_backend.invalidate_tlb = mock_flush;
    assert(intel_gen9_vm_make_ops(&intel_backend, &ops));
    assert(gpu_page_table_init(&table, &ops) == GPU_PAGE_TABLE_OK);
    assert(gpu_page_table_map(&table, va, 0x500000ULL,
              INTEL_GEN9_VM_READ | INTEL_GEN9_VM_WRITE) == GPU_PAGE_TABLE_OK);
    assert(gpu_page_table_flush(&table) == GPU_PAGE_TABLE_OK);
    assert(mapped_leaf_entry(&table, va) ==
           (0x500000ULL | INTEL_GEN9_PTE_PRESENT |
            INTEL_GEN9_PTE_WRITABLE | INTEL_GEN9_PTE_PAT));
    assert(gpu_page_table_make_vm_ops(&table, &generic_vm_ops));
    assert(generic_vm_ops.map_page(generic_vm_ops.context, va + 0x1000U,
                                   0x501000U, 1U));
    assert(generic_vm_ops.flush(generic_vm_ops.context));
    assert(generic_vm_ops.unmap_page(generic_vm_ops.context, va + 0x1000U));
    assert(generic_vm_ops.flush(generic_vm_ops.context));
    assert(gpu_page_table_destroy(&table, 1) == GPU_PAGE_TABLE_OK);
    assert(mock.frees == mock.allocations);

    memset(&mock, 0, sizeof(mock));
    memset(&table, 0, sizeof(table));
    amd_backend.context = &mock;
    amd_backend.allocate_table = mock_alloc;
    amd_backend.free_page = mock_free;
    amd_backend.invalidate_tlb = mock_flush;
    assert(amd_polaris_vm_make_ops(&amd_backend, &ops));
    assert(gpu_page_table_init_config(&table, &ops,
               amd_polaris_vm_config()) == GPU_PAGE_TABLE_OK);
    assert(table.config.level_count == 2U &&
           table.config.root_entry_count == 4U &&
           table.config.leaf_entry_count == MOCK_LARGE_TABLE_ENTRIES);
    assert(gpu_page_table_map(&table, 0x12345000ULL, 0x600000ULL,
              AMD_POLARIS_PTE_ACCESS_READ |
              AMD_POLARIS_PTE_ACCESS_WRITE) == GPU_PAGE_TABLE_OK);
    assert(gpu_page_table_flush(&table) == GPU_PAGE_TABLE_OK);
    assert(mapped_leaf_entry(&table, 0x12345000ULL) ==
           (0x600000ULL | AMD_POLARIS_PTE_VALID |
            AMD_POLARIS_PTE_SYSTEM | AMD_POLARIS_PTE_SNOOPED |
            AMD_POLARIS_PTE_READABLE | AMD_POLARIS_PTE_WRITEABLE));
    assert(gpu_page_table_destroy(&table, 1) == GPU_PAGE_TABLE_OK);
    assert(mock.frees == mock.allocations);
}

int main(void)
{
    test_mapping_lifecycle();
    test_fail_closed_on_flush_failure();
    test_vendor_vm_backends();
    return 0;
}
