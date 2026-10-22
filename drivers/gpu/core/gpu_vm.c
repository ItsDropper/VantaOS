/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_vm.h"

#include <string.h>

static size_t gpu_vm_find_free_record(const gpu_vm_t *vm)
{
    for (size_t i = 0U; i < GPU_VM_MAX_MAPPINGS; ++i)
        if (vm->mappings[i].active == 0U)
            return i;
    return GPU_VM_MAX_MAPPINGS;
}

static gpu_vm_record_t *gpu_vm_find_record(gpu_vm_t *vm,
                                           const gpu_vm_handle_t *handle)
{
    if (handle == NULL || handle->token == 0U ||
        handle->generation != vm->generation)
        return NULL;
    for (size_t i = 0U; i < GPU_VM_MAX_MAPPINGS; ++i) {
        gpu_vm_record_t *record = &vm->mappings[i];
        if (record->active != 0U && record->token == handle->token &&
            record->generation == handle->generation)
            return record;
    }
    return NULL;
}

static int gpu_vm_validate_inputs(const gpu_va_allocation_t *va,
                                  const gpu_dma_buffer_t *dma,
                                  unsigned access)
{
    if (va == NULL || va->valid == 0U || va->base == 0U ||
        va->size == 0U ||
        (va->base & (GPU_VA_PAGE_SIZE - 1U)) != 0U ||
        (va->size & (GPU_VA_PAGE_SIZE - 1U)) != 0U)
        return 0;
    if (dma == NULL || !gpu_dma_buffer_is_valid(dma) ||
        dma->allocated_size == 0U ||
        dma->allocated_size > va->size ||
        dma->allocated_size != dma->page_count * GPU_DMA_PAGE_SIZE)
        return 0;
    if ((access & (GPU_VM_ACCESS_READ | GPU_VM_ACCESS_WRITE)) == 0U ||
        (access & ~(GPU_VM_ACCESS_READ | GPU_VM_ACCESS_WRITE)) != 0U)
        return 0;
    if (va->base > UINT64_MAX - (dma->allocated_size - 1U))
        return 0;
    return 1;
}

gpu_vm_status_t gpu_vm_init(gpu_vm_t *vm, gpu_va_space_t *va_space,
                            const gpu_vm_ops_t *ops)
{
    if (vm == NULL || va_space == NULL || ops == NULL ||
        vm->initialized != 0U)
        return GPU_VM_INVALID_ARGUMENT;
    if (va_space->initialized == 0U || ops->map_page == NULL ||
        ops->unmap_page == NULL || ops->flush == NULL)
        return GPU_VM_INVALID_ARGUMENT;

    memset(vm, 0, sizeof(*vm));
    spinlock_init(&vm->lock);
    vm->va_space = va_space;
    vm->ops = *ops;
    vm->next_token = 1U;
    vm->generation = 1U;
    vm->initialized = 1U;
    return GPU_VM_OK;
}

gpu_vm_status_t gpu_vm_map_dma(gpu_vm_t *vm,
                               const gpu_va_allocation_t *va,
                               const gpu_dma_buffer_t *dma,
                               unsigned access, gpu_vm_handle_t *out_handle)
{
    uint64_t flags;
    size_t slot, mapped = 0U;
    int rollback_ok = 1;
    int pending_flush = 0;
    gpu_vm_record_t failed_record;

    if (out_handle == NULL)
        return GPU_VM_INVALID_ARGUMENT;
    out_handle->token = 0U;
    out_handle->generation = 0U;
    if (vm == NULL || vm->initialized == 0U)
        return GPU_VM_NOT_INITIALIZED;
    if (va == NULL || va->valid == 0U || va->base == 0U ||
        va->size == 0U ||
        (va->base & (GPU_VA_PAGE_SIZE - 1U)) != 0U ||
        (va->size & (GPU_VA_PAGE_SIZE - 1U)) != 0U)
        return GPU_VM_VA_INVALID;
    if (dma == NULL || !gpu_dma_buffer_is_valid(dma))
        return GPU_VM_DMA_INVALID;
    if (!gpu_vm_validate_inputs(va, dma, access))
        return GPU_VM_INVALID_ARGUMENT;

    flags = spinlock_lock_irqsave(&vm->lock);
    if (vm->faulted != 0U) {
        spinlock_unlock_irqrestore(&vm->lock, flags);
        return GPU_VM_DEVICE_FAULTED;
    }
    slot = gpu_vm_find_free_record(vm);
    if (slot == GPU_VM_MAX_MAPPINGS || vm->next_token == 0U ||
        vm->next_token == UINT64_MAX) {
        spinlock_unlock_irqrestore(&vm->lock, flags);
        return GPU_VM_TABLE_FULL;
    }

    memset(&failed_record, 0, sizeof(failed_record));
    for (size_t i = 0U; i < dma->page_count; ++i) {
        uint64_t gpu_address = va->base + (uint64_t)i * GPU_VM_PAGE_SIZE;
        if (!vm->ops.map_page(vm->ops.context, gpu_address,
                              dma->pages[i].physical_address, access))
            goto rollback;
        ++mapped;
    }
    if (!vm->ops.flush(vm->ops.context)) {
        pending_flush = 1;
        goto rollback;
    }

    vm->mappings[slot].va = va;
    vm->mappings[slot].dma = dma;
    vm->mappings[slot].token = vm->next_token++;
    vm->mappings[slot].generation = vm->generation;
    vm->mappings[slot].mapped_pages = mapped;
    vm->mappings[slot].pending_flush = 0U;
    vm->mappings[slot].active = 1U;
    ++vm->mapping_count;
    out_handle->token = vm->mappings[slot].token;
    out_handle->generation = vm->mappings[slot].generation;
    spinlock_unlock_irqrestore(&vm->lock, flags);
    return GPU_VM_OK;

rollback:
    /*
     * Undo in reverse order. If any PTE cannot be removed or invalidation
     * cannot be confirmed, retain the caller's buffers behind a retry handle.
     */
    while (mapped != 0U) {
        size_t index = mapped - 1U;
        uint64_t gpu_address = va->base + (uint64_t)index * GPU_VM_PAGE_SIZE;
        if (!vm->ops.unmap_page(vm->ops.context, gpu_address)) {
            rollback_ok = 0;
            break;
        }
        --mapped;
    }
    if (mapped == 0U && !vm->ops.flush(vm->ops.context)) {
        pending_flush = 1;
        rollback_ok = 0;
    }
    if (!rollback_ok) {
        failed_record.va = va;
        failed_record.dma = dma;
        failed_record.token = vm->next_token++;
        if (failed_record.token == 0U)
            failed_record.token = vm->next_token++;
        failed_record.generation = vm->generation;
        failed_record.mapped_pages = mapped;
        failed_record.pending_flush = (uint8_t)pending_flush;
        failed_record.active = 1U;
        vm->mappings[slot] = failed_record;
        ++vm->mapping_count;
        vm->faulted = 1U;
        out_handle->token = failed_record.token;
        out_handle->generation = failed_record.generation;
        spinlock_unlock_irqrestore(&vm->lock, flags);
        return GPU_VM_ROLLBACK_FAILED;
    }
    spinlock_unlock_irqrestore(&vm->lock, flags);
    return pending_flush ? GPU_VM_FLUSH_FAILED : GPU_VM_MAP_FAILED;
}

gpu_vm_status_t gpu_vm_unmap(gpu_vm_t *vm, gpu_vm_handle_t *handle)
{
    uint64_t flags;
    gpu_vm_record_t *record;

    if (vm == NULL || handle == NULL)
        return GPU_VM_INVALID_ARGUMENT;
    if (vm->initialized == 0U)
        return GPU_VM_NOT_INITIALIZED;

    flags = spinlock_lock_irqsave(&vm->lock);
    record = gpu_vm_find_record(vm, handle);
    if (record == NULL) {
        spinlock_unlock_irqrestore(&vm->lock, flags);
        return GPU_VM_INVALID_HANDLE;
    }

    while (record->mapped_pages != 0U) {
        size_t index = record->mapped_pages - 1U;
        uint64_t gpu_address = record->va->base +
                               (uint64_t)index * GPU_VM_PAGE_SIZE;
        if (!vm->ops.unmap_page(vm->ops.context, gpu_address)) {
            vm->faulted = 1U;
            spinlock_unlock_irqrestore(&vm->lock, flags);
            return GPU_VM_UNMAP_FAILED;
        }
        --record->mapped_pages;
        record->pending_flush = 1U;
    }

    if (record->pending_flush != 0U) {
        if (!vm->ops.flush(vm->ops.context)) {
            vm->faulted = 1U;
            spinlock_unlock_irqrestore(&vm->lock, flags);
            return GPU_VM_FLUSH_FAILED;
        }
        record->pending_flush = 0U;
    }

    memset(record, 0, sizeof(*record));
    if (vm->mapping_count != 0U)
        --vm->mapping_count;
    handle->token = 0U;
    handle->generation = 0U;

    if (vm->mapping_count == 0U)
        vm->faulted = 0U;
    spinlock_unlock_irqrestore(&vm->lock, flags);
    return GPU_VM_OK;
}

gpu_vm_status_t gpu_vm_validate(gpu_vm_t *vm)
{
    uint64_t flags;
    size_t active = 0U;

    if (vm == NULL || vm->initialized == 0U || vm->va_space == NULL)
        return GPU_VM_NOT_INITIALIZED;
    flags = spinlock_lock_irqsave(&vm->lock);
    for (size_t i = 0U; i < GPU_VM_MAX_MAPPINGS; ++i) {
        const gpu_vm_record_t *record = &vm->mappings[i];
        if (record->active == 0U)
            continue;
        ++active;
        if (record->token == 0U || record->generation != vm->generation ||
            record->va == NULL || record->dma == NULL ||
            !gpu_vm_validate_inputs(record->va, record->dma,
                                    GPU_VM_ACCESS_READ) ||
            record->mapped_pages > record->dma->page_count ||
            (record->mapped_pages == 0U && record->pending_flush == 0U)) {
            spinlock_unlock_irqrestore(&vm->lock, flags);
            return GPU_VM_CORRUPT_STATE;
        }
    }
    if (active != vm->mapping_count) {
        spinlock_unlock_irqrestore(&vm->lock, flags);
        return GPU_VM_CORRUPT_STATE;
    }
    spinlock_unlock_irqrestore(&vm->lock, flags);
    return gpu_va_validate(vm->va_space) == GPU_VA_OK
               ? GPU_VM_OK : GPU_VM_CORRUPT_STATE;
}

gpu_vm_status_t gpu_vm_destroy(gpu_vm_t *vm)
{
    uint64_t flags;
    if (vm == NULL || vm->initialized == 0U)
        return GPU_VM_NOT_INITIALIZED;
    flags = spinlock_lock_irqsave(&vm->lock);
    if (vm->mapping_count != 0U || vm->faulted != 0U) {
        spinlock_unlock_irqrestore(&vm->lock, flags);
        return GPU_VM_BUSY;
    }
    vm->initialized = 0U;
    vm->va_space = NULL;
    memset(&vm->ops, 0, sizeof(vm->ops));
    spinlock_unlock_irqrestore(&vm->lock, flags);
    return GPU_VM_OK;
}
