/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_vm.h"
#include "heap.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE 4096U
static uint8_t dma_memory[16U * PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static size_t dma_next_page;
static int fail_map_at = -1;
static int fail_unmap_at = -1;
static unsigned fail_flush_count;
static unsigned map_calls, unmap_calls, flush_calls;
static uint64_t mapped_addresses[GPU_DMA_MAX_PAGES];
static size_t mapped_count;

int heap_is_initialized(void) { return 1; }
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr) { free(ptr); }

int dma_alloc_contiguous(dma_page_t *pages, size_t count)
{
    if (pages == NULL || count == 0U ||
        count > (sizeof(dma_memory) / PAGE_SIZE) - dma_next_page)
        return 0;
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = dma_memory + (dma_next_page + i) * PAGE_SIZE;
        pages[i].physical_address = 0x800000U + (dma_next_page + i) * PAGE_SIZE;
        pages[i].size = PAGE_SIZE;
    }
    dma_next_page += count;
    return 1;
}
int dma_page_is_valid(const dma_page_t *page)
{
    return page != NULL && page->virtual_address != NULL &&
           page->size == PAGE_SIZE &&
           (((uintptr_t)page->virtual_address & (PAGE_SIZE - 1U)) == 0U) &&
           ((page->physical_address & (PAGE_SIZE - 1U)) == 0U);
}
void dma_free_contiguous(dma_page_t *pages, size_t count)
{
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = NULL;
        pages[i].physical_address = 0U;
        pages[i].size = 0U;
    }
}

static int map_page(void *ctx, uint64_t va, uint64_t pa, unsigned access)
{
    (void)ctx;
    assert((va & (PAGE_SIZE - 1U)) == 0U);
    assert((pa & (PAGE_SIZE - 1U)) == 0U);
    assert(access == GPU_VM_ACCESS_READ || access ==
           (GPU_VM_ACCESS_READ | GPU_VM_ACCESS_WRITE));
    ++map_calls;
    if (fail_map_at >= 0 && va == (uint64_t)fail_map_at)
        return 0;
    assert(mapped_count < GPU_DMA_MAX_PAGES);
    mapped_addresses[mapped_count++] = va;
    return 1;
}
static int unmap_page(void *ctx, uint64_t va)
{
    (void)ctx;
    ++unmap_calls;
    if (fail_unmap_at >= 0 && va == (uint64_t)fail_unmap_at)
        return 0;
    for (size_t i = 0U; i < mapped_count; ++i) {
        if (mapped_addresses[i] == va) {
            for (size_t j = i; j + 1U < mapped_count; ++j)
                mapped_addresses[j] = mapped_addresses[j + 1U];
            --mapped_count;
            return 1;
        }
    }
    return 0;
}
static int flush(void *ctx)
{
    (void)ctx;
    ++flush_calls;
    if (fail_flush_count != 0U) {
        --fail_flush_count;
        return 0;
    }
    return 1;
}

int main(void)
{
    gpu_va_space_t va_space = {0};
    gpu_vm_t vm = {0};
    gpu_dma_buffer_t dma = {0};
    gpu_va_allocation_t va = {0};
    gpu_vm_handle_t handle = {0}, stale_handle = {0};
    gpu_vm_ops_t ops = { .map_page = map_page, .unmap_page = unmap_page,
                         .flush = flush };

    assert(gpu_va_init(&va_space, 0x10000000ULL, 0x1000000ULL) == GPU_VA_OK);
    assert(gpu_vm_init(&vm, &va_space, &ops) == GPU_VM_OK);
    assert(gpu_vm_map_dma(&vm, NULL, &dma, GPU_VM_ACCESS_READ, &handle) ==
           GPU_VM_VA_INVALID);
    assert(gpu_dma_alloc(8192U, 32U, &dma) == GPU_DMA_OK);
    assert(gpu_va_alloc(&va_space, dma.allocated_size, GPU_VA_PAGE_SIZE,
                        GPU_VA_GUARD_ONE_PAGE, &va) == GPU_VA_OK);

    assert(gpu_vm_map_dma(&vm, &va, &dma,
                          GPU_VM_ACCESS_READ | GPU_VM_ACCESS_WRITE,
                          &handle) == GPU_VM_OK);
    assert(handle.token != 0U && mapped_count == 2U && map_calls == 2U);
    assert(gpu_vm_validate(&vm) == GPU_VM_OK);
    assert(gpu_vm_destroy(&vm) == GPU_VM_BUSY);
    assert(gpu_vm_unmap(&vm, &handle) == GPU_VM_OK);
    assert(handle.token == 0U && mapped_count == 0U);
    assert(gpu_vm_unmap(&vm, &handle) == GPU_VM_INVALID_HANDLE);
    assert(gpu_va_free(&va_space, &va) == GPU_VA_OK);
    gpu_dma_release(&dma);

    assert(gpu_dma_alloc(8192U, 32U, &dma) == GPU_DMA_OK);
    assert(gpu_va_alloc(&va_space, dma.allocated_size, GPU_VA_PAGE_SIZE,
                        0U, &va) == GPU_VA_OK);
    fail_map_at = (int)(va.base + PAGE_SIZE);
    assert(gpu_vm_map_dma(&vm, &va, &dma, GPU_VM_ACCESS_READ, &handle) ==
           GPU_VM_MAP_FAILED);
    assert(mapped_count == 0U);
    fail_map_at = -1;
    assert(gpu_va_free(&va_space, &va) == GPU_VA_OK);
    gpu_dma_release(&dma);

    assert(gpu_dma_alloc(8192U, 32U, &dma) == GPU_DMA_OK);
    assert(gpu_va_alloc(&va_space, dma.allocated_size, GPU_VA_PAGE_SIZE,
                        0U, &va) == GPU_VA_OK);
    fail_map_at = (int)(va.base + PAGE_SIZE);
    fail_unmap_at = (int)va.base;
    assert(gpu_vm_map_dma(&vm, &va, &dma, GPU_VM_ACCESS_READ, &handle) ==
           GPU_VM_ROLLBACK_FAILED);
    assert(handle.token != 0U && vm.faulted && mapped_count == 1U);
    assert(gpu_vm_map_dma(&vm, &va, &dma, GPU_VM_ACCESS_READ, &(gpu_vm_handle_t){0}) ==
           GPU_VM_DEVICE_FAULTED);
    fail_map_at = -1;
    fail_unmap_at = -1;
    assert(gpu_vm_unmap(&vm, &handle) == GPU_VM_OK);
    assert(!vm.faulted && mapped_count == 0U);
    assert(gpu_va_free(&va_space, &va) == GPU_VA_OK);
    gpu_dma_release(&dma);

    assert(gpu_dma_alloc(4096U, 32U, &dma) == GPU_DMA_OK);
    assert(gpu_va_alloc(&va_space, dma.allocated_size, GPU_VA_PAGE_SIZE,
                        0U, &va) == GPU_VA_OK);
    assert(gpu_vm_map_dma(&vm, &va, &dma, GPU_VM_ACCESS_READ, &handle) ==
           GPU_VM_OK);
    stale_handle = handle;
    fail_unmap_at = (int)va.base;
    assert(gpu_vm_unmap(&vm, &handle) == GPU_VM_UNMAP_FAILED);
    assert(vm.faulted && mapped_count == 1U);
    fail_unmap_at = -1;
    fail_flush_count = 1U;
    assert(gpu_vm_unmap(&vm, &handle) == GPU_VM_FLUSH_FAILED);
    assert(vm.faulted && mapped_count == 0U);
    assert(gpu_vm_unmap(&vm, &handle) == GPU_VM_OK);
    assert(!vm.faulted);
    assert(gpu_va_free(&va_space, &va) == GPU_VA_OK);
    gpu_dma_release(&dma);

    assert(gpu_vm_validate(&vm) == GPU_VM_OK);
    assert(gpu_vm_destroy(&vm) == GPU_VM_OK);

    assert(gpu_vm_init(&vm, &va_space, &ops) == GPU_VM_OK);
    assert(gpu_dma_alloc(4096U, 32U, &dma) == GPU_DMA_OK);
    assert(gpu_va_alloc(&va_space, dma.allocated_size, GPU_VA_PAGE_SIZE,
                        0U, &va) == GPU_VA_OK);
    assert(gpu_vm_map_dma(&vm, &va, &dma, GPU_VM_ACCESS_READ, &handle) ==
           GPU_VM_OK);
    assert(handle.generation != stale_handle.generation);
    assert(gpu_vm_unmap(&vm, &stale_handle) == GPU_VM_INVALID_HANDLE);
    assert(gpu_vm_unmap(&vm, &handle) == GPU_VM_OK);
    assert(gpu_va_free(&va_space, &va) == GPU_VA_OK);
    gpu_dma_release(&dma);
    assert(gpu_vm_destroy(&vm) == GPU_VM_OK);
    return 0;
}
