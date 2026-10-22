/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_DMA_SYNC_H
#define VANTA_GPU_DMA_SYNC_H

#include "gpu_dma.h"

typedef enum gpu_dma_owner {
    GPU_DMA_OWNER_CPU = 0,
    GPU_DMA_OWNER_DEVICE,
    GPU_DMA_OWNER_SHARED
} gpu_dma_owner_t;

typedef enum gpu_dma_sync_status {
    GPU_DMA_SYNC_OK = 0,
    GPU_DMA_SYNC_INVALID_ARGUMENT,
    GPU_DMA_SYNC_NOT_INITIALIZED,
    GPU_DMA_SYNC_INVALID_RANGE,
    GPU_DMA_SYNC_WRONG_OWNER,
    GPU_DMA_SYNC_BUFFER_INVALID
} gpu_dma_sync_status_t;

typedef struct gpu_dma_sync {
    gpu_dma_buffer_t *buffer;
    uintptr_t cpu_address;
    uint64_t gpu_address;
    size_t size;
    size_t page_count;
    uint64_t allocation_id;
    gpu_dma_owner_t owner;
    uint8_t initialized;
} gpu_dma_sync_t;

gpu_dma_sync_status_t gpu_dma_sync_init(gpu_dma_sync_t *sync,
                                        gpu_dma_buffer_t *buffer);
gpu_dma_sync_status_t gpu_dma_sync_for_device(gpu_dma_sync_t *sync,
                                              size_t offset, size_t length);
gpu_dma_sync_status_t gpu_dma_sync_for_shared(gpu_dma_sync_t *sync,
                                              size_t offset, size_t length);
gpu_dma_sync_status_t gpu_dma_sync_for_cpu(gpu_dma_sync_t *sync,
                                            size_t offset, size_t length);
gpu_dma_sync_status_t gpu_dma_sync_destroy(gpu_dma_sync_t *sync);

#endif
