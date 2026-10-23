/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_memory.h"
#include "heap.h"

#include <string.h>

static int ranges_overlap(uint64_t a, size_t a_size,
                          uint64_t b, size_t b_size)
{
    return a < b + (uint64_t)b_size && b < a + (uint64_t)a_size;
}

static int slot_available(const amd_polaris_memory_runtime_t *runtime,
                          uint64_t address, size_t size)
{
    uint64_t limit = AMD_POLARIS_MEMORY_VA_BASE +
                     AMD_POLARIS_MEMORY_VA_SIZE;
    if (address < AMD_POLARIS_MEMORY_VA_BASE || address >= limit ||
        (uint64_t)size > limit - address)
        return 0;
    for (size_t i = 0U; i < AMD_POLARIS_MEMORY_MAX_ALLOCATIONS; ++i) {
        const amd_polaris_memory_slot_t *slot = &runtime->slots[i];
        if (slot->allocation != NULL &&
            ranges_overlap(address, size, slot->address, slot->size))
            return 0;
    }
    return 1;
}

static int find_free_slot(const amd_polaris_memory_runtime_t *runtime)
{
    for (size_t i = 0U; i < AMD_POLARIS_MEMORY_MAX_ALLOCATIONS; ++i)
        if (runtime->slots[i].allocation == NULL)
            return (int)i;
    return -1;
}

static int find_free_address(const amd_polaris_memory_runtime_t *runtime,
                             size_t size, uint64_t *out_address)
{
    uint64_t candidate = AMD_POLARIS_MEMORY_VA_BASE;
    const uint64_t limit = AMD_POLARIS_MEMORY_VA_BASE +
                           AMD_POLARIS_MEMORY_VA_SIZE;

    while (candidate < limit && (uint64_t)size <= limit - candidate) {
        uint64_t next = candidate;
        int collision = 0;
        for (size_t i = 0U; i < AMD_POLARIS_MEMORY_MAX_ALLOCATIONS; ++i) {
            const amd_polaris_memory_slot_t *slot = &runtime->slots[i];
            if (slot->allocation != NULL &&
                ranges_overlap(candidate, size, slot->address, slot->size)) {
                uint64_t end = slot->address + (uint64_t)slot->size;
                if (end > next)
                    next = end;
                collision = 1;
            }
        }
        if (!collision) {
            *out_address = candidate;
            return 1;
        }
        if (next <= candidate)
            return 0;
        candidate = (next + GPU_VM_PAGE_SIZE - 1U) &
                    ~((uint64_t)GPU_VM_PAGE_SIZE - 1U);
    }
    return 0;
}

static int remember_allocation(amd_polaris_memory_runtime_t *runtime,
                               amd_polaris_gpu_allocation_t *allocation,
                               int slot)
{
    if (slot < 0 || slot >= (int)AMD_POLARIS_MEMORY_MAX_ALLOCATIONS)
        return 0;
    runtime->slots[slot].allocation = allocation;
    runtime->slots[slot].address = allocation->gpu_address;
    runtime->slots[slot].size = allocation->dma.allocated_size;
    return 1;
}

static void release_allocation(amd_polaris_gpu_allocation_t *allocation)
{
    gpu_dma_release(&allocation->dma);
    if (allocation->mapped_pages != NULL)
        kfree(allocation->mapped_pages);
    memset(allocation, 0, sizeof(*allocation));
}

amd_polaris_memory_status_t amd_polaris_memory_init(
    amd_polaris_memory_runtime_t *runtime, const gpu_vm_ops_t *vm_ops)
{
    if (runtime == NULL || vm_ops == NULL ||
        vm_ops->map_page == NULL || vm_ops->unmap_page == NULL ||
        vm_ops->flush == NULL || runtime->initialized != 0U)
        return AMD_POLARIS_MEMORY_INVALID_ARGUMENT;
    memset(runtime, 0, sizeof(*runtime));
    runtime->vm_ops = *vm_ops;
    runtime->initialized = 1U;
    return AMD_POLARIS_MEMORY_OK;
}

amd_polaris_memory_status_t amd_polaris_memory_alloc(
    amd_polaris_memory_runtime_t *runtime, size_t size, uint64_t gpu_address,
    amd_polaris_gpu_allocation_t *out_allocation)
{
    amd_polaris_gpu_allocation_t allocation = {0};
    uint64_t address = gpu_address;
    int slot;
    size_t mapped_count = 0U;

    if (runtime == NULL || out_allocation == NULL ||
        runtime->initialized == 0U || out_allocation->mapped != 0U ||
        out_allocation->dma.acquired != 0U || size == 0U ||
        size > GPU_DMA_MAX_BYTES)
        return AMD_POLARIS_MEMORY_INVALID_ARGUMENT;
    if (runtime->faulted != 0U)
        return AMD_POLARIS_MEMORY_NOT_READY;
    slot = find_free_slot(runtime);
    if (slot < 0)
        return AMD_POLARIS_MEMORY_VA_BUSY;

    if (gpu_dma_alloc(size, 32U, &allocation.dma) != GPU_DMA_OK)
        return AMD_POLARIS_MEMORY_DMA_FAILED;
    allocation.page_count = allocation.dma.page_count;
    allocation.mapped_pages = kmalloc(allocation.page_count);
    if (allocation.mapped_pages == NULL) {
        gpu_dma_release(&allocation.dma);
        return AMD_POLARIS_MEMORY_DMA_FAILED;
    }
    memset(allocation.mapped_pages, 0, allocation.page_count);

    if (address == 0U) {
        if (!find_free_address(runtime, allocation.dma.allocated_size,
                               &address)) {
            release_allocation(&allocation);
            return AMD_POLARIS_MEMORY_VA_BUSY;
        }
    } else if ((address & (GPU_VM_PAGE_SIZE - 1U)) != 0U ||
               !slot_available(runtime, address,
                               allocation.dma.allocated_size)) {
        release_allocation(&allocation);
        return AMD_POLARIS_MEMORY_VA_BUSY;
    }

    allocation.runtime = runtime;
    allocation.gpu_address = address;
    for (size_t i = 0U; i < allocation.page_count; ++i) {
        uint64_t gpu_va = address + (uint64_t)i * GPU_VM_PAGE_SIZE;
        uint64_t physical = allocation.dma.pages[i].physical_address;
        if (!runtime->vm_ops.map_page(runtime->vm_ops.context, gpu_va,
                physical, GPU_VM_ACCESS_READ | GPU_VM_ACCESS_WRITE))
            break;
        allocation.mapped_pages[i] = 1U;
        ++mapped_count;
    }

    if (mapped_count != allocation.page_count) {
        int rollback_ok = 1;
        for (size_t i = allocation.page_count; i > 0U; --i) {
            size_t index = i - 1U;
            if (allocation.mapped_pages[index] != 0U) {
                uint64_t va = address + (uint64_t)index * GPU_VM_PAGE_SIZE;
                if (runtime->vm_ops.unmap_page(runtime->vm_ops.context, va))
                    allocation.mapped_pages[index] = 0U;
                else
                    rollback_ok = 0;
            }
        }
        if (!runtime->vm_ops.flush(runtime->vm_ops.context))
            rollback_ok = 0;
        if (!rollback_ok) {
            allocation.mapped = 1U;
            *out_allocation = allocation;
            (void)remember_allocation(runtime, out_allocation, slot);
            runtime->faulted = 1U;
            return AMD_POLARIS_MEMORY_ROLLBACK_FAILED;
        }
        release_allocation(&allocation);
        return AMD_POLARIS_MEMORY_MAP_FAILED;
    }

    if (!runtime->vm_ops.flush(runtime->vm_ops.context)) {
        allocation.mapped = 1U;
        *out_allocation = allocation;
        (void)remember_allocation(runtime, out_allocation, slot);
        runtime->faulted = 1U;
        return AMD_POLARIS_MEMORY_FLUSH_FAILED;
    }

    allocation.mapped = 1U;
    *out_allocation = allocation;
    (void)remember_allocation(runtime, out_allocation, slot);
    return AMD_POLARIS_MEMORY_OK;
}

amd_polaris_memory_status_t amd_polaris_memory_free(
    amd_polaris_gpu_allocation_t *allocation)
{
    amd_polaris_memory_runtime_t *runtime;
    int slot = -1;

    if (allocation == NULL || allocation->mapped == 0U ||
        allocation->runtime == NULL || allocation->dma.acquired == 0U ||
        allocation->mapped_pages == NULL ||
        allocation->page_count != allocation->dma.page_count)
        return AMD_POLARIS_MEMORY_INVALID_ARGUMENT;
    runtime = allocation->runtime;
    if (runtime->initialized == 0U)
        return AMD_POLARIS_MEMORY_NOT_READY;

    for (size_t i = 0U; i < AMD_POLARIS_MEMORY_MAX_ALLOCATIONS; ++i)
        if (runtime->slots[i].allocation == allocation) {
            slot = (int)i;
            break;
        }
    if (slot < 0 || runtime->slots[slot].address != allocation->gpu_address ||
        runtime->slots[slot].size != allocation->dma.allocated_size)
        return AMD_POLARIS_MEMORY_INVALID_ARGUMENT;

    for (size_t i = allocation->page_count; i > 0U; --i) {
        size_t index = i - 1U;
        if (allocation->mapped_pages[index] != 0U) {
            uint64_t va = allocation->gpu_address +
                          (uint64_t)index * GPU_VM_PAGE_SIZE;
            if (!runtime->vm_ops.unmap_page(runtime->vm_ops.context, va)) {
                runtime->faulted = 1U;
                return AMD_POLARIS_MEMORY_UNMAP_FAILED;
            }
            allocation->mapped_pages[index] = 0U;
        }
    }
    if (!runtime->vm_ops.flush(runtime->vm_ops.context)) {
        runtime->faulted = 1U;
        return AMD_POLARIS_MEMORY_FLUSH_FAILED;
    }

    release_allocation(allocation);
    memset(&runtime->slots[slot], 0, sizeof(runtime->slots[slot]));
    return AMD_POLARIS_MEMORY_OK;
}

amd_polaris_memory_status_t amd_polaris_memory_shutdown(
    amd_polaris_memory_runtime_t *runtime, int device_quiesced)
{
    if (runtime == NULL || runtime->initialized == 0U)
        return AMD_POLARIS_MEMORY_INVALID_ARGUMENT;
    if (!device_quiesced)
        return AMD_POLARIS_MEMORY_NOT_READY;
    for (size_t i = 0U; i < AMD_POLARIS_MEMORY_MAX_ALLOCATIONS; ++i)
        if (runtime->slots[i].allocation != NULL)
            return AMD_POLARIS_MEMORY_VA_BUSY;
    memset(runtime, 0, sizeof(*runtime));
    return AMD_POLARIS_MEMORY_OK;
}
