/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_dma_sync.h"

#include <string.h>

static int gpu_dma_sync_valid(const gpu_dma_sync_t *sync)
{
    const gpu_dma_buffer_t *buffer;
    if (sync == NULL || sync->initialized == 0U || sync->buffer == NULL)
        return 0;
    buffer = sync->buffer;
    return gpu_dma_buffer_is_valid(buffer) &&
           buffer->cpu_address == (void *)sync->cpu_address &&
           buffer->gpu_address == sync->gpu_address &&
           buffer->size == sync->size &&
           buffer->page_count == sync->page_count &&
           buffer->allocation_id == sync->allocation_id;
}

static int gpu_dma_sync_range_valid(const gpu_dma_sync_t *sync,
                                    size_t offset, size_t length)
{
    return length != 0U && offset <= sync->size &&
           length <= sync->size - offset;
}

static void gpu_dma_sync_release_barrier(void)
{
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __asm__ volatile("mfence" ::: "memory");
}

static void gpu_dma_sync_acquire_barrier(void)
{
    __asm__ volatile("mfence" ::: "memory");
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}

gpu_dma_sync_status_t gpu_dma_sync_init(gpu_dma_sync_t *sync,
                                        gpu_dma_buffer_t *buffer)
{
    if (sync == NULL || buffer == NULL)
        return GPU_DMA_SYNC_INVALID_ARGUMENT;
    if (sync->initialized != 0U)
        return GPU_DMA_SYNC_INVALID_ARGUMENT;
    if (!gpu_dma_buffer_is_valid(buffer))
        return GPU_DMA_SYNC_BUFFER_INVALID;

    memset(sync, 0, sizeof(*sync));
    sync->buffer = buffer;
    sync->cpu_address = (uintptr_t)buffer->cpu_address;
    sync->gpu_address = buffer->gpu_address;
    sync->size = buffer->size;
    sync->page_count = buffer->page_count;
    sync->allocation_id = buffer->allocation_id;
    sync->owner = GPU_DMA_OWNER_CPU;
    sync->initialized = 1U;
    return GPU_DMA_SYNC_OK;
}

gpu_dma_sync_status_t gpu_dma_sync_for_device(gpu_dma_sync_t *sync,
                                              size_t offset, size_t length)
{
    if (sync == NULL)
        return GPU_DMA_SYNC_INVALID_ARGUMENT;
    if (sync->initialized == 0U)
        return GPU_DMA_SYNC_NOT_INITIALIZED;
    if (!gpu_dma_sync_valid(sync))
        return GPU_DMA_SYNC_BUFFER_INVALID;
    if (!gpu_dma_sync_range_valid(sync, offset, length))
        return GPU_DMA_SYNC_INVALID_RANGE;
    if (sync->owner != GPU_DMA_OWNER_CPU)
        return GPU_DMA_SYNC_WRONG_OWNER;

    gpu_dma_sync_release_barrier();
    sync->owner = GPU_DMA_OWNER_DEVICE;
    return GPU_DMA_SYNC_OK;
}

gpu_dma_sync_status_t gpu_dma_sync_for_shared(gpu_dma_sync_t *sync,
                                              size_t offset, size_t length)
{
    if (sync == NULL)
        return GPU_DMA_SYNC_INVALID_ARGUMENT;
    if (sync->initialized == 0U)
        return GPU_DMA_SYNC_NOT_INITIALIZED;
    if (!gpu_dma_sync_valid(sync))
        return GPU_DMA_SYNC_BUFFER_INVALID;
    if (!gpu_dma_sync_range_valid(sync, offset, length))
        return GPU_DMA_SYNC_INVALID_RANGE;
    if (sync->owner != GPU_DMA_OWNER_CPU)
        return GPU_DMA_SYNC_WRONG_OWNER;

    /* Shared queues still need release ordering before device consumption. */
    gpu_dma_sync_release_barrier();
    sync->owner = GPU_DMA_OWNER_SHARED;
    return GPU_DMA_SYNC_OK;
}

gpu_dma_sync_status_t gpu_dma_sync_for_cpu(gpu_dma_sync_t *sync,
                                            size_t offset, size_t length)
{
    if (sync == NULL)
        return GPU_DMA_SYNC_INVALID_ARGUMENT;
    if (sync->initialized == 0U)
        return GPU_DMA_SYNC_NOT_INITIALIZED;
    if (!gpu_dma_sync_valid(sync))
        return GPU_DMA_SYNC_BUFFER_INVALID;
    if (!gpu_dma_sync_range_valid(sync, offset, length))
        return GPU_DMA_SYNC_INVALID_RANGE;
    if (sync->owner != GPU_DMA_OWNER_DEVICE &&
        sync->owner != GPU_DMA_OWNER_SHARED)
        return GPU_DMA_SYNC_WRONG_OWNER;

    gpu_dma_sync_acquire_barrier();
    sync->owner = GPU_DMA_OWNER_CPU;
    return GPU_DMA_SYNC_OK;
}

gpu_dma_sync_status_t gpu_dma_sync_destroy(gpu_dma_sync_t *sync)
{
    if (sync == NULL)
        return GPU_DMA_SYNC_INVALID_ARGUMENT;
    if (sync->initialized == 0U)
        return GPU_DMA_SYNC_NOT_INITIALIZED;
    if (!gpu_dma_sync_valid(sync))
        return GPU_DMA_SYNC_BUFFER_INVALID;
    if (sync->owner != GPU_DMA_OWNER_CPU)
        return GPU_DMA_SYNC_WRONG_OWNER;
    memset(sync, 0, sizeof(*sync));
    return GPU_DMA_SYNC_OK;
}
