/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_DMA_SYNC_H
#define VANTA_NVX_DMA_SYNC_H
#include <stddef.h>
#include <stdint.h>
#include "nvx_dma.h"
typedef enum nvx_dma_owner {
    NVX_DMA_OWNER_CPU=0, NVX_DMA_OWNER_DEVICE=1,
    /* CPU and GPU may concurrently access explicitly shared coherent memory. */
    NVX_DMA_OWNER_SHARED=2
} nvx_dma_owner_t;
typedef enum nvx_dma_sync_status {
    NVX_DMA_SYNC_OK=0, NVX_DMA_SYNC_INVALID_ARGUMENT,
    NVX_DMA_SYNC_NOT_INITIALIZED, NVX_DMA_SYNC_INVALID_RANGE,
    NVX_DMA_SYNC_WRONG_OWNER, NVX_DMA_SYNC_BUFFER_INVALID
} nvx_dma_sync_status_t;
typedef struct nvx_dma_sync {
    nvx_dma_buffer_t *buffer;
    uintptr_t cpu_address;
    uint64_t gpu_address;
    size_t size;
    size_t page_count;
    uint64_t allocation_id;
    nvx_dma_owner_t owner;
    uint8_t initialized;
} nvx_dma_sync_t;
/* Zero-initialize before init. Tracks exclusive or shared coherent ownership. */
nvx_dma_sync_status_t nvx_dma_sync_init(nvx_dma_sync_t *, nvx_dma_buffer_t *);
nvx_dma_sync_status_t nvx_dma_sync_for_device(nvx_dma_sync_t *, size_t offset, size_t length);
/* Keep coherent queues shared while CPU and GPU use separate queue endpoints. */
nvx_dma_sync_status_t nvx_dma_sync_for_shared(nvx_dma_sync_t *, size_t offset, size_t length);
nvx_dma_sync_status_t nvx_dma_sync_for_cpu(nvx_dma_sync_t *, size_t offset, size_t length);
nvx_dma_sync_status_t nvx_dma_sync_destroy(nvx_dma_sync_t *);
#endif
