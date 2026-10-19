/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_object_memory.h"

static void nvx_memory_zero_handle(nvx_memory_handle_t *handle)
{
    handle->token = 0U;
    handle->gpu_virtual_address = 0U;
    handle->size = 0U;
    handle->slot = 0U;
    handle->generation = 0U;
}

static int nvx_memory_valid_kind(nvx_memory_kind_t kind)
{
    return kind == NVX_MEMORY_STAGING ||
           kind == NVX_MEMORY_FIRMWARE_IMAGE ||
           kind == NVX_MEMORY_COMMAND_STORAGE;
}

static int nvx_memory_handle_matches(const nvx_memory_manager_t *manager,
                                     const nvx_memory_handle_t *handle,
                                     const nvx_memory_slot_t **out_slot)
{
    const nvx_memory_slot_t *slot;
    if (handle == 0 || handle->token == 0U ||
        handle->generation != manager->generation ||
        handle->slot >= NVX_MEMORY_MAX_OBJECTS)
        return 0;
    slot = &manager->slots[handle->slot];
    if (slot->state != NVX_MEMORY_SLOT_READY ||
        slot->token != handle->token ||
        slot->generation != handle->generation ||
        slot->va.base != handle->gpu_virtual_address ||
        slot->requested_size != handle->size)
        return 0;
    if (out_slot != 0)
        *out_slot = slot;
    return 1;
}

nvx_memory_status_t nvx_memory_init(nvx_memory_manager_t *manager,
                                    nvx_gpuva_space_t *va_space,
                                    unsigned address_bits)
{
    if (manager == 0 || va_space == 0)
        return NVX_MEMORY_INVALID_ARGUMENT;
    if (manager->initialized)
        return NVX_MEMORY_INVALID_ARGUMENT;
    if (!va_space->initialized)
        return NVX_MEMORY_ADDRESS_SPACE_FAILED;
    if (address_bits != 32U && address_bits != 64U)
        return NVX_MEMORY_INVALID_ARGUMENT;

    spinlock_init(&manager->lock);
    manager->va_space = va_space;
    manager->next_token = 1U;
    ++manager->generation;
    if (manager->generation == 0U)
        manager->generation = 1U;
    manager->address_bits = address_bits;
    for (size_t i = 0; i < NVX_MEMORY_MAX_OBJECTS; ++i) {
        manager->slots[i].state = NVX_MEMORY_SLOT_FREE;
        manager->slots[i].token = 0U;
        manager->slots[i].requested_size = 0U;
        manager->slots[i].generation = 0U;
        manager->slots[i].kind = 0U;
        manager->slots[i].va.valid = 0U;
        manager->slots[i].dma.acquired = 0U;
        manager->slots[i].dma.page_count = 0U;
    }
    manager->initialized = 1U;
    return NVX_MEMORY_OK;
}

nvx_memory_status_t nvx_memory_alloc(nvx_memory_manager_t *manager,
                                     size_t size, nvx_memory_kind_t kind,
                                     nvx_memory_handle_t *out_handle)
{
    size_t slot_index = NVX_MEMORY_MAX_OBJECTS;
    uint64_t token;
    nvx_gpuva_allocation_t va = {0};
    nvx_dma_buffer_t dma = {0};
    nvx_gpuva_status_t va_status;
    nvx_dma_status_t dma_status;

    if (out_handle == 0 || manager == 0)
        return NVX_MEMORY_INVALID_ARGUMENT;
    nvx_memory_zero_handle(out_handle);
    if (!manager->initialized)
        return NVX_MEMORY_NOT_INITIALIZED;
    if (size == 0U || !nvx_memory_valid_kind(kind))
        return NVX_MEMORY_INVALID_ARGUMENT;

    spinlock_lock(&manager->lock);
    if (manager->next_token == 0U || manager->next_token == UINT64_MAX) {
        spinlock_unlock(&manager->lock);
        return NVX_MEMORY_CORRUPT_STATE;
    }
    for (size_t i = 0; i < NVX_MEMORY_MAX_OBJECTS; ++i) {
        if (manager->slots[i].state == NVX_MEMORY_SLOT_FREE) {
            slot_index = i;
            manager->slots[i].state = NVX_MEMORY_SLOT_RESERVED;
            manager->slots[i].token = manager->next_token++;
            manager->slots[i].generation = manager->generation;
            manager->slots[i].requested_size = size;
            manager->slots[i].kind = (uint8_t)kind;
            break;
        }
    }
    if (slot_index == NVX_MEMORY_MAX_OBJECTS) {
        spinlock_unlock(&manager->lock);
        return NVX_MEMORY_OUT_OF_OBJECTS;
    }
    token = manager->slots[slot_index].token;
    spinlock_unlock(&manager->lock);

    /*
     * Never hold the manager spinlock while entering another allocator. This
     * prevents lock-order inversion and keeps potentially slow PMM work outside
     * the object-table critical section.
     */
    va_status = nvx_gpuva_alloc(manager->va_space, (uint64_t)size,
                                NVX_GPUVA_PAGE_SIZE, 1U, &va);
    if (va_status != NVX_GPUVA_OK) {
        spinlock_lock(&manager->lock);
        manager->slots[slot_index].state = NVX_MEMORY_SLOT_FREE;
        manager->slots[slot_index].token = 0U;
        spinlock_unlock(&manager->lock);
        return NVX_MEMORY_ADDRESS_SPACE_FAILED;
    }

    dma_status = nvx_dma_alloc(size, manager->address_bits, &dma);
    if (dma_status != NVX_DMA_OK) {
        (void)nvx_gpuva_free(manager->va_space, &va);
        spinlock_lock(&manager->lock);
        manager->slots[slot_index].state = NVX_MEMORY_SLOT_FREE;
        manager->slots[slot_index].token = 0U;
        spinlock_unlock(&manager->lock);
        return NVX_MEMORY_ALLOCATION_FAILED;
    }

    spinlock_lock(&manager->lock);
    if (manager->slots[slot_index].state != NVX_MEMORY_SLOT_RESERVED ||
        manager->slots[slot_index].token != token) {
        spinlock_unlock(&manager->lock);
        nvx_dma_release(&dma);
        (void)nvx_gpuva_free(manager->va_space, &va);
        return NVX_MEMORY_CORRUPT_STATE;
    }
    manager->slots[slot_index].va = va;
    manager->slots[slot_index].dma = dma;
    manager->slots[slot_index].state = NVX_MEMORY_SLOT_READY;
    out_handle->token = token;
    out_handle->gpu_virtual_address = va.base;
    out_handle->size = (uint64_t)size;
    out_handle->slot = (uint32_t)slot_index;
    out_handle->generation = manager->generation;
    spinlock_unlock(&manager->lock);
    return NVX_MEMORY_OK;
}

nvx_memory_status_t nvx_memory_get_info(nvx_memory_manager_t *manager,
                                         const nvx_memory_handle_t *handle,
                                         nvx_memory_info_t *out_info)
{
    const nvx_memory_slot_t *slot;
    if (out_info != 0)
        *out_info = (nvx_memory_info_t){0};
    if (manager == 0 || handle == 0 || out_info == 0)
        return NVX_MEMORY_INVALID_ARGUMENT;
    if (!manager->initialized)
        return NVX_MEMORY_NOT_INITIALIZED;

    spinlock_lock(&manager->lock);
    if (!nvx_memory_handle_matches(manager, handle, &slot)) {
        spinlock_unlock(&manager->lock);
        return NVX_MEMORY_STALE_HANDLE;
    }
    out_info->cpu_address = slot->dma.cpu_address;
    out_info->gpu_virtual_address = slot->va.base;
    out_info->size = slot->requested_size;
    out_info->kind = (nvx_memory_kind_t)slot->kind;
    spinlock_unlock(&manager->lock);
    return NVX_MEMORY_OK;
}

nvx_memory_status_t nvx_memory_cpu_address(nvx_memory_manager_t *manager,
                                           const nvx_memory_handle_t *handle,
                                           void **out_address)
{
    const nvx_memory_slot_t *slot;
    if (out_address != 0)
        *out_address = 0;
    if (manager == 0 || handle == 0 || out_address == 0)
        return NVX_MEMORY_INVALID_ARGUMENT;
    if (!manager->initialized)
        return NVX_MEMORY_NOT_INITIALIZED;

    spinlock_lock(&manager->lock);
    if (!nvx_memory_handle_matches(manager, handle, &slot)) {
        spinlock_unlock(&manager->lock);
        return NVX_MEMORY_STALE_HANDLE;
    }
    *out_address = slot->dma.cpu_address;
    spinlock_unlock(&manager->lock);
    return NVX_MEMORY_OK;
}

nvx_memory_status_t nvx_memory_release(nvx_memory_manager_t *manager,
                                       nvx_memory_handle_t *handle)
{
    nvx_memory_slot_t *slot;
    nvx_dma_buffer_t *dma;
    nvx_gpuva_allocation_t *va;
    if (manager == 0 || handle == 0)
        return NVX_MEMORY_INVALID_ARGUMENT;
    if (!manager->initialized)
        return NVX_MEMORY_NOT_INITIALIZED;

    spinlock_lock(&manager->lock);
    if (!nvx_memory_handle_matches(manager, handle, 0)) {
        spinlock_unlock(&manager->lock);
        return NVX_MEMORY_STALE_HANDLE;
    }
    slot = &manager->slots[handle->slot];
    slot->state = NVX_MEMORY_SLOT_RELEASING;
    dma = &slot->dma;
    va = &slot->va;
    spinlock_unlock(&manager->lock);

    nvx_dma_release(dma);
    nvx_gpuva_status_t va_status = nvx_gpuva_free(manager->va_space, va);

    spinlock_lock(&manager->lock);
    slot->token = 0U;
    slot->requested_size = 0U;
    slot->generation = 0U;
    slot->kind = 0U;
    slot->state = NVX_MEMORY_SLOT_FREE;
    spinlock_unlock(&manager->lock);
    nvx_memory_zero_handle(handle);

    return va_status == NVX_GPUVA_OK ? NVX_MEMORY_OK
                                     : NVX_MEMORY_CORRUPT_STATE;
}

nvx_memory_status_t nvx_memory_validate(nvx_memory_manager_t *manager)
{
    size_t ready = 0U;
    size_t reserved = 0U;
    if (manager == 0)
        return NVX_MEMORY_INVALID_ARGUMENT;
    if (!manager->initialized)
        return NVX_MEMORY_NOT_INITIALIZED;

    spinlock_lock(&manager->lock);
    for (size_t i = 0; i < NVX_MEMORY_MAX_OBJECTS; ++i) {
        const nvx_memory_slot_t *slot = &manager->slots[i];
        if (slot->state == NVX_MEMORY_SLOT_READY) {
            ++ready;
            if (slot->token == 0U || slot->generation != manager->generation ||
                !slot->va.valid || !slot->dma.acquired ||
                slot->requested_size == 0U ||
                slot->dma.size != slot->requested_size) {
                spinlock_unlock(&manager->lock);
                return NVX_MEMORY_CORRUPT_STATE;
            }
        } else if (slot->state == NVX_MEMORY_SLOT_RESERVED ||
                   slot->state == NVX_MEMORY_SLOT_RELEASING) {
            ++reserved;
        } else if (slot->state != NVX_MEMORY_SLOT_FREE) {
            spinlock_unlock(&manager->lock);
            return NVX_MEMORY_CORRUPT_STATE;
        }
    }
    (void)ready;
    (void)reserved;
    spinlock_unlock(&manager->lock);
    return nvx_gpuva_validate(manager->va_space) == NVX_GPUVA_OK
        ? NVX_MEMORY_OK : NVX_MEMORY_CORRUPT_STATE;
}
