/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_vm_runtime.h"
#include "gpu_dma_page_table.h"
#include "heap.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MOCK_MMIO_BYTES 0x2000U
#define REG_INVALIDATE_RESPONSE 0x147CU
#define POLARIS_LEAF_BYTES (32U * 1024U * 1024U)

static unsigned char dma_memory[GPU_DMA_MAX_BYTES]
    __attribute__((aligned(GPU_DMA_PAGE_SIZE)));
static size_t dma_used;
static uint32_t registers[MOCK_MMIO_BYTES / sizeof(uint32_t)];

int heap_is_initialized(void) { return 1; }
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr) { free(ptr); }

int dma_alloc_contiguous(dma_page_t *pages, size_t count)
{
    size_t bytes = count * GPU_DMA_PAGE_SIZE;
    if (pages == NULL || count == 0U || count > GPU_DMA_MAX_PAGES ||
        bytes > sizeof(dma_memory) - dma_used)
        return 0;

    memset(dma_memory + dma_used, 0, bytes);
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = dma_memory + dma_used +
                                   i * GPU_DMA_PAGE_SIZE;
        pages[i].physical_address = 0x20000000ULL + dma_used +
                                    i * GPU_DMA_PAGE_SIZE;
        pages[i].size = GPU_DMA_PAGE_SIZE;
    }
    dma_used += bytes;
    return 1;
}

int dma_page_is_valid(const dma_page_t *page)
{
    return page != NULL && page->virtual_address != NULL &&
        page->size == GPU_DMA_PAGE_SIZE &&
        (((uintptr_t)page->virtual_address & (GPU_DMA_PAGE_SIZE - 1U)) == 0U) &&
        ((page->physical_address & (GPU_DMA_PAGE_SIZE - 1U)) == 0U);
}

void dma_free_contiguous(dma_page_t *pages, size_t count)
{
    assert(pages != NULL && count != 0U && count <= GPU_DMA_MAX_PAGES);
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = NULL;
        pages[i].physical_address = 0U;
        pages[i].size = 0U;
    }
}

int main(void)
{
    gpu_mmio_region_t mmio = {
        .base = (volatile uint8_t *)registers,
        .length = sizeof(registers)
    };
    amd_polaris_vm_runtime_t runtime = {0};
    gpu_page_table_t table = {0};
    gpu_vm_ops_t vm_ops;

    registers[REG_INVALIDATE_RESPONSE / sizeof(uint32_t)] =
        1U << AMD_POLARIS_VM_CONTEXT1_ID;

    assert(amd_polaris_vm_runtime_configure_dma(
        &runtime, &mmio, 0x300000U, 16U) == AMD_POLARIS_VM_RUNTIME_OK);
    assert(amd_polaris_vm_runtime_init_table(&runtime, &table, 0) ==
           AMD_POLARIS_VM_RUNTIME_DEVICE_BUSY);
    assert(table.initialized == 0U);

    assert(amd_polaris_vm_runtime_init_table(&runtime, &table, 1) ==
           AMD_POLARIS_VM_RUNTIME_OK);
    assert(table.initialized != 0U);
    assert(table.config.leaf_entry_count == (1U << 22U));

    assert(amd_polaris_vm_runtime_make_vm_ops(&runtime, &table, &vm_ops) ==
           AMD_POLARIS_VM_RUNTIME_OK);
    assert(vm_ops.map_page(vm_ops.context, 0x1000U, 0x400000U,
                           GPU_VM_ACCESS_READ | GPU_VM_ACCESS_WRITE));
    assert(dma_used >= GPU_DMA_PAGE_SIZE + POLARIS_LEAF_BYTES);
    assert(vm_ops.flush(vm_ops.context));
    assert(vm_ops.unmap_page(vm_ops.context, 0x1000U));
    assert(vm_ops.flush(vm_ops.context));

    assert(amd_polaris_vm_runtime_shutdown(&runtime, &table, 0) ==
           AMD_POLARIS_VM_RUNTIME_DEVICE_BUSY);
    assert(amd_polaris_vm_runtime_shutdown(&runtime, &table, 1) ==
           AMD_POLARIS_VM_RUNTIME_OK);
    assert(table.initialized == 0U);
    assert(runtime.root_physical == 0U);
    return 0;
}
