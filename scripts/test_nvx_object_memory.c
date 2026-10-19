/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_object_memory.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define TABLE_PAGE_COUNT 256U
static nvx_gpuvm_t gpuvm;
static nvx_memory_manager_t manager;
static uint8_t dma_memory[NVX_DMA_MAX_BYTES * NVX_MEMORY_MAX_OBJECTS]
    __attribute__((aligned(NVX_DMA_PAGE_SIZE)));
static uint8_t table_memory[TABLE_PAGE_COUNT][NVX_GPUVM_PAGE_SIZE]
    __attribute__((aligned(NVX_GPUVM_PAGE_SIZE)));
static uint8_t fake_mmio;
static size_t next_dma_byte;
static unsigned next_table_page;
static unsigned alloc_calls;
static unsigned free_calls;
static unsigned table_free_calls;
static int fail_alloc;
static uint64_t physical_base = 0x10000000ULL;
static uint32_t flush_busy_reads;
static int fail_flush;

int dma_alloc_contiguous(dma_page_t *pages, size_t count)
{
    if (fail_alloc || pages == 0 || count == 0U ||
        count > NVX_DMA_MAX_PAGES ||
        next_dma_byte + count * NVX_DMA_PAGE_SIZE > sizeof(dma_memory))
        return 0;
    ++alloc_calls;
    for (size_t i = 0; i < count; ++i) {
        pages[i].virtual_address = dma_memory + next_dma_byte;
        pages[i].physical_address = physical_base + next_dma_byte;
        pages[i].size = NVX_DMA_PAGE_SIZE;
        next_dma_byte += NVX_DMA_PAGE_SIZE;
    }
    return 1;
}

int dma_alloc_page(dma_page_t *out)
{
    if (out == 0 || next_table_page >= TABLE_PAGE_COUNT)
        return 0;
    out->virtual_address = table_memory[next_table_page];
    out->physical_address = 0x08000000ULL +
                            (uint64_t)next_table_page * NVX_GPUVM_PAGE_SIZE;
    out->size = NVX_GPUVM_PAGE_SIZE;
    ++next_table_page;
    return 1;
}

int dma_page_is_valid(const dma_page_t *page)
{
    return page != 0 && page->virtual_address != 0 &&
           page->size == NVX_GPUVM_PAGE_SIZE &&
           (((uintptr_t)page->virtual_address &
             (NVX_GPUVM_PAGE_SIZE - 1U)) == 0U) &&
           ((page->physical_address & (NVX_GPUVM_PAGE_SIZE - 1U)) == 0U);
}

void dma_free_page(dma_page_t *page)
{
    assert(page != 0 && page->virtual_address != 0);
    ++table_free_calls;
    page->virtual_address = 0;
    page->physical_address = 0;
    page->size = 0;
}

void dma_free_contiguous(dma_page_t *pages, size_t count)
{
    assert(pages != 0 && count > 0U && count <= NVX_DMA_MAX_PAGES);
    ++free_calls;
    for (size_t i = 0; i < count; ++i) {
        pages[i].virtual_address = 0;
        pages[i].physical_address = 0;
        pages[i].size = 0;
    }
}

nvx_mmio_status_t nvx_gpuvm_test_mmio_write32(
    const nvx_mmio_region_t *region, size_t offset, uint32_t value)
{
    assert(region != 0 && region->base == &fake_mmio);
    assert(offset == 0xB830A0U || offset == 0xB830A4U ||
           offset == 0xB830B0U);
    (void)value;
    if (offset == 0xB830B0U)
        flush_busy_reads = 1U;
    return NVX_MMIO_OK;
}

nvx_mmio_status_t nvx_gpuvm_test_mmio_read32(
    const nvx_mmio_region_t *region, size_t offset, uint32_t *out)
{
    assert(region != 0 && region->base == &fake_mmio && out != 0);
    if (offset != 0xB830B0U || fail_flush)
        return NVX_MMIO_OUT_OF_RANGE;
    if (flush_busy_reads != 0U) {
        --flush_busy_reads;
        *out = 0x80000001U;
    } else {
        *out = 1U;
    }
    return NVX_MMIO_OK;
}

static void init_gpuvm(void)
{
    nvx_mmio_region_t bar0 = {.base = &fake_mmio, .length = 0xB830B4U};
    memset(&gpuvm, 0, sizeof(gpuvm));
    assert(nvx_gpuvm_init(&gpuvm, 0x100000000ULL,
                          16ULL * 1024ULL * 1024ULL, &bar0) == NVX_GPUVM_OK);
}

int main(void)
{
    nvx_memory_handle_t a = {0}, b = {0}, forged;
    nvx_memory_info_t info;
    void *cpu = (void *)(uintptr_t)1U;
    uint64_t free_before = 0U, free_after = 0U;
    size_t ranges = 0U, allocations = 0U;

    memset(&manager, 0, sizeof(manager));
    assert(nvx_memory_init(&manager, &gpuvm, 64U) ==
           NVX_MEMORY_ADDRESS_SPACE_FAILED);
    init_gpuvm();
    assert(nvx_memory_init(&manager, &gpuvm, 64U) == NVX_MEMORY_OK);
    assert(nvx_memory_alloc(&manager, 0U, NVX_MEMORY_STAGING, &a) ==
           NVX_MEMORY_INVALID_ARGUMENT);
    assert(nvx_memory_alloc(&manager, 4096U, (nvx_memory_kind_t)99, &a) ==
           NVX_MEMORY_INVALID_ARGUMENT);
    assert(nvx_memory_alloc(&manager, 4096U, NVX_MEMORY_STAGING, &a) ==
           NVX_MEMORY_OK);
    assert(a.token != 0U && a.gpu_virtual_address != 0U);
    assert(nvx_memory_cpu_address(&manager, &a, &cpu) == NVX_MEMORY_OK);
    assert(cpu != 0 && cpu != (void *)(uintptr_t)1U);
    assert(nvx_memory_get_info(&manager, &a, &info) == NVX_MEMORY_OK);
    assert(info.kind == NVX_MEMORY_STAGING && info.size == 4096U);
    assert(info.gpu_virtual_address == a.gpu_virtual_address &&
           info.cpu_address == cpu);
    memset(cpu, 0xA5, 4096U);
    assert(nvx_memory_validate(&manager) == NVX_MEMORY_OK);

    forged = a;
    forged.token++;
    assert(nvx_memory_cpu_address(&manager, &forged, &cpu) ==
           NVX_MEMORY_STALE_HANDLE);
    assert(cpu == 0);
    assert(nvx_memory_release(&manager, &forged) == NVX_MEMORY_STALE_HANDLE);

    assert(nvx_memory_alloc(&manager, 8193U, NVX_MEMORY_COMMAND_STORAGE, &b) ==
           NVX_MEMORY_OK);
    assert(nvx_memory_validate(&manager) == NVX_MEMORY_OK);
    assert(nvx_memory_release(&manager, &a) == NVX_MEMORY_OK);
    assert(nvx_memory_release(&manager, &a) == NVX_MEMORY_STALE_HANDLE);
    assert(nvx_memory_release(&manager, &b) == NVX_MEMORY_OK);
    assert(nvx_memory_validate(&manager) == NVX_MEMORY_OK);
    assert(alloc_calls == 2U && free_calls == 2U);

    assert(nvx_gpuvm_destroy(&gpuvm) == NVX_GPUVM_OK);
    memset(&manager, 0, sizeof(manager));
    init_gpuvm();
    assert(nvx_memory_init(&manager, &gpuvm, 32U) == NVX_MEMORY_OK);
    physical_base = 0x100000000ULL;
    assert(nvx_gpuva_get_stats(&gpuvm.va_space, &free_before, &ranges,
                               &allocations) == NVX_GPUVA_OK);
    assert(nvx_memory_alloc(&manager, 4096U, NVX_MEMORY_STAGING, &a) ==
           NVX_MEMORY_ALLOCATION_FAILED);
    assert(nvx_gpuva_get_stats(&gpuvm.va_space, &free_after, &ranges,
                               &allocations) == NVX_GPUVA_OK);
    assert(free_after == free_before && allocations == 0U);
    fail_alloc = 1;
    assert(nvx_memory_alloc(&manager, 4096U, NVX_MEMORY_STAGING, &a) ==
           NVX_MEMORY_ALLOCATION_FAILED);
    fail_alloc = 0;
    assert(nvx_gpuva_get_stats(&gpuvm.va_space, &free_after, &ranges,
                               &allocations) == NVX_GPUVA_OK);
    assert(free_after == free_before && allocations == 0U);
    assert(nvx_gpuvm_destroy(&gpuvm) == NVX_GPUVM_OK);

    /* A failed live page-table flush must quarantine, never free, DMA pages. */
    memset(&manager, 0, sizeof(manager));
    init_gpuvm();
    assert(nvx_memory_init(&manager, &gpuvm, 64U) == NVX_MEMORY_OK);
    assert(nvx_gpuvm_activate(&gpuvm) == NVX_GPUVM_OK);
    fail_flush = 1;
    unsigned frees_before_fault = free_calls;
    assert(nvx_memory_alloc(&manager, 4096U, NVX_MEMORY_STAGING, &a) ==
           NVX_MEMORY_GPUVM_FAILED);
    assert(manager.slots[0].state == NVX_MEMORY_SLOT_QUARANTINED);
    assert(manager.slots[0].dma.acquired && manager.slots[0].mapping.mapped);
    assert(free_calls == frees_before_fault);
    assert(nvx_memory_validate(&manager) == NVX_MEMORY_GPUVM_FAILED);
    assert(nvx_gpuvm_destroy(&gpuvm) == NVX_GPUVM_DEVICE_FAULTED);
    return 0;
}
