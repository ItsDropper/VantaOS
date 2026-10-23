/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_vm_hw.h"
#include "amd_polaris_vm_runtime.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define MOCK_TABLE_PAGES GPU_PAGE_TABLE_MAX_PAGES
#define MOCK_POLARIS_LEAF_ENTRIES (1U << 22U)

static uint64_t runtime_small_tables[MOCK_TABLE_PAGES][GPU_PAGE_TABLE_ENTRIES];
static uint64_t runtime_large_leaf[MOCK_POLARIS_LEAF_ENTRIES];

typedef struct runtime_allocator {
    uint8_t in_use[MOCK_TABLE_PAGES];
    size_t allocations;
    size_t frees;
} runtime_allocator_t;

static int runtime_allocate(void *context, size_t entries,
                            gpu_page_table_page_t *out_page)
{
    runtime_allocator_t *allocator = context;
    if (entries == 0U || entries > MOCK_POLARIS_LEAF_ENTRIES)
        return 0;
    for (size_t i = 0U; i < MOCK_TABLE_PAGES; ++i) {
        if (!allocator->in_use[i]) {
            allocator->in_use[i] = 1U;
            out_page->entries = entries > GPU_PAGE_TABLE_ENTRIES ?
                                runtime_large_leaf : runtime_small_tables[i];
            out_page->physical_address = 0x100000ULL + i * 0x100000ULL;
            out_page->entry_count = entries;
            out_page->allocation_size = entries * sizeof(uint64_t);
            out_page->cookie = (void *)(uintptr_t)(i + 1U);
            ++allocator->allocations;
            return 1;
        }
    }
    return 0;
}

static void runtime_free(void *context, gpu_page_table_page_t *page)
{
    runtime_allocator_t *allocator = context;
    size_t index = (size_t)(uintptr_t)page->cookie - 1U;
    assert(index < MOCK_TABLE_PAGES && allocator->in_use[index]);
    allocator->in_use[index] = 0U;
    ++allocator->frees;
    memset(page, 0, sizeof(*page));
}

#define MOCK_MMIO_BYTES 0x2000U
#define REG_INVALIDATE_REQUEST 0x1478U
#define REG_INVALIDATE_RESPONSE 0x147CU
#define REG_CONTEXT1_CNTL 0x1414U
#define REG_CONTEXT1_BASE 0x1540U
#define REG_CONTEXT1_START 0x1560U
#define REG_CONTEXT1_END 0x1580U
#define REG_CONTEXT1_FAULT 0x151CU

static uint32_t registers[MOCK_MMIO_BYTES / sizeof(uint32_t)];

static gpu_mmio_region_t region(size_t length)
{
    gpu_mmio_region_t mmio;
    mmio.base = (volatile uint8_t *)registers;
    mmio.length = length;
    return mmio;
}

static uint32_t read_reg(uint32_t offset)
{
    return registers[offset / sizeof(uint32_t)];
}

static void test_install_root(void)
{
    gpu_mmio_region_t mmio = region(MOCK_MMIO_BYTES);
    memset(registers, 0, sizeof(registers));
    registers[REG_INVALIDATE_RESPONSE / 4U] = 1U << AMD_POLARIS_VM_CONTEXT1_ID;

    assert(amd_polaris_vm_install_root(&mmio, 0x200000ULL, 0U,
               AMD_POLARIS_VM_DEFAULT_RANGE_SIZE, 0x300000ULL, 4U, 0) ==
           AMD_POLARIS_VM_HW_DEVICE_BUSY);
    assert(read_reg(REG_CONTEXT1_BASE) == 0U);

    assert(amd_polaris_vm_install_root(&mmio, 0x200001ULL, 0U,
               AMD_POLARIS_VM_DEFAULT_RANGE_SIZE, 0x300000ULL, 4U, 1) ==
           AMD_POLARIS_VM_HW_INVALID_ADDRESS);
    assert(amd_polaris_vm_install_root(&mmio, 0x200000ULL, 0U,
               AMD_POLARIS_VM_DEFAULT_RANGE_SIZE, 0x300000ULL, 4U, 1) ==
           AMD_POLARIS_VM_HW_OK);

    assert(read_reg(REG_CONTEXT1_BASE) == 0x200U);
    assert(read_reg(REG_CONTEXT1_START) == 0U);
    assert(read_reg(REG_CONTEXT1_END) == 0xFFFFFFU);
    assert(read_reg(REG_CONTEXT1_FAULT) == 0x300U);
    assert((read_reg(REG_CONTEXT1_CNTL) & 0x13U) == 0x13U);
    assert(read_reg(REG_INVALIDATE_REQUEST) ==
           (1U << AMD_POLARIS_VM_CONTEXT1_ID));
}

static void test_invalidate_bounds_and_timeout(void)
{
    gpu_mmio_region_t mmio = region(MOCK_MMIO_BYTES);
    memset(registers, 0, sizeof(registers));
    assert(amd_polaris_vm_invalidate(&mmio, 3U) ==
           AMD_POLARIS_VM_HW_INVALIDATE_TIMEOUT);
    registers[REG_INVALIDATE_RESPONSE / 4U] =
        1U << AMD_POLARIS_VM_CONTEXT1_ID;
    assert(amd_polaris_vm_invalidate(&mmio, 3U) == AMD_POLARIS_VM_HW_OK);

    mmio.length = REG_INVALIDATE_RESPONSE;
    assert(amd_polaris_vm_invalidate(&mmio, 3U) ==
           AMD_POLARIS_VM_HW_INVALID_ARGUMENT);
    assert(amd_polaris_vm_invalidate(&mmio, 0U) ==
           AMD_POLARIS_VM_HW_INVALID_ARGUMENT);
}


static void test_runtime_end_to_end_vm(void)
{
    gpu_mmio_region_t mmio = region(MOCK_MMIO_BYTES);
    runtime_allocator_t allocator;
    amd_polaris_vm_runtime_t runtime;
    gpu_page_table_t table;
    gpu_vm_ops_t vm_ops;

    memset(registers, 0, sizeof(registers));
    memset(&allocator, 0, sizeof(allocator));
    memset(&runtime, 0, sizeof(runtime));
    memset(&table, 0, sizeof(table));
    registers[REG_INVALIDATE_RESPONSE / 4U] =
        1U << AMD_POLARIS_VM_CONTEXT1_ID;

    assert(amd_polaris_vm_runtime_configure(&runtime, &allocator,
               runtime_allocate, runtime_free, &mmio, 0x300000U, 4U) ==
           AMD_POLARIS_VM_RUNTIME_OK);
    assert(amd_polaris_vm_runtime_init_table(&runtime, &table, 0) ==
           AMD_POLARIS_VM_RUNTIME_DEVICE_BUSY);
    assert(allocator.allocations == 0U);

    assert(amd_polaris_vm_runtime_init_table(&runtime, &table, 1) ==
           AMD_POLARIS_VM_RUNTIME_OK);
    assert(table.config.level_count == 2U);
    assert(table.config.root_entry_count == 4U);
    assert(table.config.leaf_entry_count == MOCK_POLARIS_LEAF_ENTRIES);
    assert(amd_polaris_vm_runtime_make_vm_ops(&runtime, &table, &vm_ops) ==
           AMD_POLARIS_VM_RUNTIME_OK);

    assert(vm_ops.map_page(vm_ops.context, 0x1000U, 0x400000U,
                           GPU_VM_ACCESS_READ | GPU_VM_ACCESS_WRITE));
    assert(vm_ops.flush(vm_ops.context));
    assert(vm_ops.unmap_page(vm_ops.context, 0x1000U));
    assert(vm_ops.flush(vm_ops.context));

    assert(amd_polaris_vm_runtime_shutdown(&runtime, &table, 0) ==
           AMD_POLARIS_VM_RUNTIME_DEVICE_BUSY);
    assert(amd_polaris_vm_runtime_shutdown(&runtime, &table, 1) ==
           AMD_POLARIS_VM_RUNTIME_OK);
    assert(allocator.allocations == 2U && allocator.frees == 2U);
    assert((read_reg(REG_CONTEXT1_CNTL) & 1U) == 0U);
}

int main(void)
{
    test_install_root();
    test_invalidate_bounds_and_timeout();
    test_runtime_end_to_end_vm();
    return 0;
}
