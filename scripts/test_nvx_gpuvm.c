/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gpuvm.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define TEST_PAGE_COUNT 256U
#define TEST_VA_START 0x1000000000000ULL
#define TEST_VA_SIZE  (16ULL * 1024ULL * 1024ULL)

static nvx_gpuvm_t vm;
static uint8_t page_pool[TEST_PAGE_COUNT][NVX_GPUVM_PAGE_SIZE]
    __attribute__((aligned(NVX_GPUVM_PAGE_SIZE)));
static unsigned next_page;
static unsigned freed_pages;
static uint32_t registers[3];
static unsigned flush_busy_reads;
static unsigned flush_writes;
static int fail_flush;
static uint8_t fake_mmio;

int dma_alloc_page(dma_page_t *out)
{
    if (out == 0 || next_page >= TEST_PAGE_COUNT)
        return 0;
    out->virtual_address = page_pool[next_page];
    out->physical_address = 0x10000000ULL +
                            (uint64_t)next_page * NVX_GPUVM_PAGE_SIZE;
    out->size = NVX_GPUVM_PAGE_SIZE;
    ++next_page;
    return 1;
}

void dma_free_page(dma_page_t *page)
{
    assert(page != 0 && page->virtual_address != 0);
    ++freed_pages;
    page->virtual_address = 0;
    page->physical_address = 0;
    page->size = 0;
}

int dma_page_is_valid(const dma_page_t *page)
{
    return page != 0 && page->virtual_address != 0 &&
           page->size == NVX_GPUVM_PAGE_SIZE &&
           (((uintptr_t)page->virtual_address &
             (NVX_GPUVM_PAGE_SIZE - 1U)) == 0U) &&
           ((page->physical_address & (NVX_GPUVM_PAGE_SIZE - 1U)) == 0U);
}

nvx_mmio_status_t nvx_gpuvm_test_mmio_write32(
    const nvx_mmio_region_t *region, size_t offset, uint32_t value)
{
    assert(region != 0 && region->base == &fake_mmio);
    if (offset == 0xB830A0U)
        registers[0] = value;
    else if (offset == 0xB830A4U)
        registers[1] = value;
    else if (offset == 0xB830B0U) {
        registers[2] = value;
        ++flush_writes;
        flush_busy_reads = 1U;
    } else {
        return NVX_MMIO_OUT_OF_RANGE;
    }
    return NVX_MMIO_OK;
}

nvx_mmio_status_t nvx_gpuvm_test_mmio_read32(
    const nvx_mmio_region_t *region, size_t offset, uint32_t *out)
{
    assert(region != 0 && region->base == &fake_mmio && out != 0);
    if (offset != 0xB830B0U)
        return NVX_MMIO_OUT_OF_RANGE;
    if (fail_flush)
        return NVX_MMIO_OUT_OF_RANGE;
    if (flush_busy_reads != 0U) {
        --flush_busy_reads;
        *out = 0x80000001U;
    } else {
        *out = 1U;
    }
    return NVX_MMIO_OK;
}

static uint16_t test_index_for_level(uint64_t va, uint8_t level)
{
    switch (level) {
        case 4: return (uint16_t)((va >> 47U) & 0x3U);
        case 3: return (uint16_t)((va >> 38U) & 0x1FFU);
        case 2: return (uint16_t)((va >> 29U) & 0x1FFU);
        case 1: return (uint16_t)((va >> 21U) & 0xFFU);
        default: return (uint16_t)((va >> 12U) & 0x1FFU);
    }
}

static size_t test_find_child(uint16_t parent, uint16_t slot)
{
    for (size_t i = 0; i < NVX_GPUVM_MAX_TABLES; ++i)
        if (vm.tables[i].active && vm.tables[i].parent_index == parent &&
            vm.tables[i].parent_slot == slot)
            return i;
    return NVX_GPUVM_MAX_TABLES;
}

static void test_invalid_init(void)
{
    nvx_gpuvm_t empty = {0};
    nvx_mmio_region_t bad = {0};
    assert(nvx_gpuvm_init(0, TEST_VA_START, TEST_VA_SIZE, &bad) ==
           NVX_GPUVM_INVALID_ARGUMENT);
    assert(nvx_gpuvm_init(&empty, 0U, TEST_VA_SIZE, &bad) ==
           NVX_GPUVM_INVALID_ARGUMENT);
}

int main(void)
{
    nvx_mmio_region_t bar0 = {
        .base = &fake_mmio,
        .length = 0xB830B4U
    };
    nvx_gpuvm_mapping_t mapping = {0}, second = {0}, invalid = {0};
    uint64_t physical[2] = {0x30000000ULL, 0x30001000ULL};
    uint64_t free_bytes;
    size_t ranges, allocations;

    memset(&vm, 0, sizeof(vm));
    test_invalid_init();
    assert(nvx_gpuvm_init(&vm, TEST_VA_START, TEST_VA_SIZE, &bar0) ==
           NVX_GPUVM_OK);
    assert(vm.table_count == 1U && vm.root_published == 0U);
    assert(nvx_gpuvm_validate(&vm) == NVX_GPUVM_OK);

    assert(nvx_gpuvm_reserve(&vm, 8192U, NVX_GPUVM_PAGE_SIZE, 1U,
                             &mapping) == NVX_GPUVM_OK);
    assert(mapping.valid && !mapping.mapped);
    assert(nvx_gpuvm_map_pages(&vm, &mapping, physical, 1U,
                               NVX_GPUVM_ACCESS_READ) ==
           NVX_GPUVM_INVALID_RANGE);
    uint64_t bad_physical[2] = {physical[0], physical[1] + 1U};
    assert(nvx_gpuvm_map_pages(&vm, &mapping, bad_physical, 2U,
                               NVX_GPUVM_ACCESS_READ) ==
           NVX_GPUVM_PAGE_INVALID);
    assert(!mapping.mapped);
    assert(nvx_gpuvm_map_pages(&vm, &mapping, physical, 2U,
                               NVX_GPUVM_ACCESS_READ) == NVX_GPUVM_OK);
    assert(mapping.mapped);
    assert(nvx_gpuvm_map_pages(&vm, &mapping, physical, 2U,
                               NVX_GPUVM_ACCESS_READ) ==
           NVX_GPUVM_ALREADY_MAPPED);
    assert(nvx_gpuvm_validate(&vm) == NVX_GPUVM_OK);

    /* Walk to the leaf and verify the 4 KiB system-coherent PTE format. */
    uint16_t leaf = vm.root_index;
    for (uint8_t level = 4U; level > 0U; --level) {
        uint16_t slot = test_index_for_level(mapping.base, level);
        size_t child = test_find_child(leaf, slot);
        assert(child < NVX_GPUVM_MAX_TABLES);
        uint64_t *pde = (uint64_t *)((uintptr_t)vm.tables[leaf].backing.virtual_address +
                                     (uintptr_t)slot * sizeof(uint64_t));
        uint64_t expected_pde =
            (vm.tables[child].backing.physical_address >> 8U) |
            (2ULL << 32U) | (1ULL << 34U);
        assert(*pde == expected_pde);
        leaf = (uint16_t)child;
    }
    uint64_t *entry = (uint64_t *)((uintptr_t)vm.tables[leaf].backing.virtual_address +
        (uintptr_t)test_index_for_level(mapping.base, 0U) * sizeof(uint64_t));
    const uint64_t expected_pte = (physical[0] >> 8U) | 1ULL | (1ULL << 1U) |
                                  (1ULL << 2U) | (1ULL << 32U) |
                                  (2ULL << 33U);
    assert(entry != 0 && *entry == expected_pte);

    assert(nvx_gpuvm_activate(&vm) == NVX_GPUVM_OK);
    assert(vm.root_published && registers[0] ==
           (uint32_t)(vm.tables[vm.root_index].backing.physical_address >> 8U));
    assert(registers[1] == 0U && registers[2] == 0x80000001U);
    assert(nvx_gpuvm_set_quiesced(&vm, 0) == NVX_GPUVM_OK);
    assert(nvx_gpuvm_reserve(&vm, 4096U, NVX_GPUVM_PAGE_SIZE, 0U,
                             &second) == NVX_GPUVM_OK);
    assert(nvx_gpuvm_map_pages(&vm, &second, physical, 1U,
                               NVX_GPUVM_ACCESS_READ) == NVX_GPUVM_BUSY);
    assert(nvx_gpuvm_set_quiesced(&vm, 1) == NVX_GPUVM_OK);
    assert(nvx_gpuvm_map_pages(&vm, &second, physical, 1U,
                               NVX_GPUVM_ACCESS_READ |
                               NVX_GPUVM_ACCESS_WRITE) == NVX_GPUVM_OK);
    assert(nvx_gpuvm_validate(&vm) == NVX_GPUVM_OK);

    assert(nvx_gpuvm_release(&vm, &mapping) == NVX_GPUVM_OK);
    assert(!mapping.valid && !mapping.mapped);
    assert(nvx_gpuvm_release(&vm, &mapping) == NVX_GPUVM_STALE_HANDLE);
    assert(nvx_gpuvm_release(&vm, &second) == NVX_GPUVM_OK);
    assert(nvx_gpuva_get_stats(&vm.va_space, &free_bytes, &ranges,
                               &allocations) == NVX_GPUVA_OK);
    assert(free_bytes == TEST_VA_SIZE && allocations == 0U);
    assert(nvx_gpuvm_validate(&vm) == NVX_GPUVM_OK);

    unsigned tables_before_destroy = vm.table_count;
    unsigned freed_before_destroy = freed_pages;
    assert(nvx_gpuvm_destroy(&vm) == NVX_GPUVM_OK);
    assert(freed_pages - freed_before_destroy == tables_before_destroy && !vm.initialized);

    /* A failed hardware flush faults the VM and deliberately retains tables. */
    assert(nvx_gpuvm_init(&vm, TEST_VA_START, TEST_VA_SIZE, &bar0) ==
           NVX_GPUVM_OK);
    assert(nvx_gpuvm_activate(&vm) == NVX_GPUVM_OK);
    unsigned freed_before_fault = freed_pages;
    fail_flush = 1;
    assert(nvx_gpuvm_flush(&vm) == NVX_GPUVM_FLUSH_TIMEOUT);
    assert(vm.faulted);
    assert(nvx_gpuvm_release(&vm, &invalid) == NVX_GPUVM_DEVICE_FAULTED);
    assert(nvx_gpuvm_destroy(&vm) == NVX_GPUVM_DEVICE_FAULTED);
    assert(freed_pages == freed_before_fault);
    return 0;
}
