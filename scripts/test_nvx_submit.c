/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_submit.h"
#include "heap.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static nvx_gpuvm_t gpuvm;
static uint8_t table_pages[64][NVX_GPUVM_PAGE_SIZE] __attribute__((aligned(NVX_GPUVM_PAGE_SIZE)));
static unsigned next_table_page;
static nvx_memory_manager_t manager;
static uint8_t dma_memory[1024U * 1024U]
    __attribute__((aligned(NVX_DMA_PAGE_SIZE)));
static size_t next_page;

int heap_is_initialized(void) { return 1; }
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr) { free(ptr); }

int dma_alloc_page(dma_page_t *out)
{
    if (out == 0 || next_table_page >= 64U)
        return 0;
    out->virtual_address = table_pages[next_table_page];
    out->physical_address = 0x10000000ULL +
                           (uint64_t)next_table_page * NVX_GPUVM_PAGE_SIZE;
    out->size = NVX_GPUVM_PAGE_SIZE;
    ++next_table_page;
    return 1;
}

void dma_free_page(dma_page_t *page)
{
    assert(page != 0 && page->virtual_address != 0);
    page->virtual_address = 0;
    page->physical_address = 0;
    page->size = 0;
}

static nvx_gpuvm_status_t test_publish_root(void *context,
                                                    uint64_t root_physical)
{
    (void)context;
    if (root_physical != 0U &&
        ((root_physical & 0xFFFU) != 0U ||
         root_physical >= (1ULL << NVX_GPUVM_PA_BITS)))
        return NVX_GPUVM_ADDRESS_WIDTH;
    return NVX_GPUVM_OK;
}

static nvx_gpuvm_status_t test_invalidate(void *context)
{
    (void)context;
    return NVX_GPUVM_OK;
}

int dma_alloc_contiguous(dma_page_t *pages, size_t count)
{
    if (pages == 0 || count == 0U ||
        next_page + count * NVX_DMA_PAGE_SIZE > sizeof(dma_memory))
        return 0;
    for (size_t i = 0; i < count; ++i) {
        pages[i].virtual_address = dma_memory + next_page;
        pages[i].physical_address = 0x20000000ULL + next_page;
        pages[i].size = NVX_DMA_PAGE_SIZE;
        next_page += NVX_DMA_PAGE_SIZE;
    }
    return 1;
}

int dma_page_is_valid(const dma_page_t *page)
{
    return page != 0 && page->virtual_address != 0 &&
           page->size == NVX_DMA_PAGE_SIZE &&
           (((uintptr_t)page->virtual_address &
             (NVX_DMA_PAGE_SIZE - 1U)) == 0U) &&
           ((page->physical_address & (NVX_DMA_PAGE_SIZE - 1U)) == 0U);
}

void dma_free_contiguous(dma_page_t *pages, size_t count)
{
    assert(pages != 0 && count > 0U && count <= NVX_DMA_MAX_PAGES);
    for (size_t i = 0; i < count; ++i) {
        pages[i].virtual_address = 0;
        pages[i].physical_address = 0;
        pages[i].size = 0;
    }
}

int main(void)
{
    nvx_memory_handle_t command = {0}, target = {0};
    nvx_relocation_t relocations[2] = {{0}};
    nvx_submit_report_t report;

    nvx_gpuvm_hw_ops_t hw = {
        .context = 0,
        .publish_root = test_publish_root,
        .invalidate = test_invalidate
    };
    memset(&gpuvm, 0, sizeof(gpuvm));
    memset(&manager, 0, sizeof(manager));
    assert(nvx_gpuvm_init(&gpuvm, 0x10000000ULL,
                          64ULL * 1024ULL * 1024ULL, &hw) == NVX_GPUVM_OK);
    assert(nvx_memory_init(&manager, &gpuvm, 64U) == NVX_MEMORY_OK);
    assert(nvx_memory_alloc(&manager, 4096U, NVX_MEMORY_COMMAND_STORAGE,
                            &command) == NVX_MEMORY_OK);
    assert(nvx_memory_alloc(&manager, 8192U, NVX_MEMORY_STAGING,
                            &target) == NVX_MEMORY_OK);

    relocations[0].command_offset = 0U;
    relocations[0].target_offset = 0U;
    relocations[0].target_span = 64U;
    relocations[0].width = NVX_RELOC_ADDRESS64;
    relocations[0].access = NVX_RELOC_READ;
    relocations[0].target = target;
    relocations[1].command_offset = 8U;
    relocations[1].target_offset = 4096U;
    relocations[1].target_span = 32U;
    relocations[1].width = NVX_RELOC_ADDRESS64;
    relocations[1].access = NVX_RELOC_READ_WRITE;
    relocations[1].target = target;

    assert(nvx_submit_validate_batch(&manager, &target, 64U, relocations,
                                     2U, &report) ==
           NVX_SUBMIT_INVALID_COMMAND_RANGE);
    assert(nvx_submit_validate_batch(&manager, &command, 64U, relocations,
                                     2U, &report) == NVX_SUBMIT_OK);
    assert(report.relocation_count == 2U && report.address64_count == 2U);
    assert(report.read_relocation_count == 2U &&
           report.write_relocation_count == 1U);
    assert(report.command_gpu_address == command.gpu_virtual_address);

    /*
     * A failed batch must not leak partial success counters to callers.
     * This matters when callers use the report for diagnostics or recovery.
     */
    relocations[1].command_offset = 4U;
    relocations[1].width = NVX_RELOC_ADDRESS32;
    report.relocation_count = 99U;
    report.command_gpu_address = UINT64_MAX;
    assert(nvx_submit_validate_batch(&manager, &command, 64U, relocations,
                                     2U, &report) ==
           NVX_SUBMIT_RELOCATION_OVERLAP);
    assert(report.relocation_count == 0U);
    assert(report.command_gpu_address == 0U);
    assert(report.command_bytes == 0U);

    /* Handles are opaque: modifying their size must invalidate the handle. */
    nvx_memory_handle_t forged_command = command;
    forged_command.size += 4096U;
    report.relocation_count = 99U;
    assert(nvx_submit_validate_batch(&manager, &forged_command, 64U,
                                     relocations, 2U, &report) ==
           NVX_SUBMIT_MEMORY_ERROR);
    assert(report.relocation_count == 0U);
    assert(report.command_gpu_address == 0U);

    relocations[1].command_offset = 4U;
    relocations[1].width = NVX_RELOC_ADDRESS32;
    assert(nvx_submit_validate_batch(&manager, &command, 64U, relocations,
                                     2U, &report) ==
           NVX_SUBMIT_RELOCATION_OVERLAP);
    relocations[1].command_offset = 8U;
    relocations[1].width = NVX_RELOC_ADDRESS64;
    relocations[0].command_offset = 2U;
    assert(nvx_submit_validate_batch(&manager, &command, 64U, relocations,
                                     2U, &report) ==
           NVX_SUBMIT_INVALID_RELOCATION);
    relocations[0].command_offset = 0U;
    relocations[1].target_offset = 8190U;
    assert(nvx_submit_validate_batch(&manager, &command, 64U, relocations,
                                     2U, &report) ==
           NVX_SUBMIT_TARGET_INVALID);
    relocations[1].target_offset = 4096U;
    assert(nvx_submit_validate_batch(&manager, &command, 3U, relocations,
                                     2U, &report) ==
           NVX_SUBMIT_INVALID_COMMAND_RANGE);
    assert(nvx_submit_validate_batch(&manager, &command, 64U, 0, 1U,
                                     &report) == NVX_SUBMIT_INVALID_ARGUMENT);
    assert(nvx_submit_validate_batch(&manager, &command, 64U, relocations,
                                     NVX_SUBMIT_MAX_RELOCATIONS + 1U,
                                     &report) == NVX_SUBMIT_TOO_MANY_RELOCATIONS);

    assert(nvx_memory_release(&manager, &command) == NVX_MEMORY_OK);
    assert(nvx_memory_release(&manager, &target) == NVX_MEMORY_OK);
    assert(nvx_gpuvm_destroy(&gpuvm) == NVX_GPUVM_OK);
    return 0;
}
