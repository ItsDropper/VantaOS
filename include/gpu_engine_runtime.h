/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_ENGINE_RUNTIME_H
#define VANTA_GPU_ENGINE_RUNTIME_H

#include "gpu_dma_sync.h"
#include "gpu_engine.h"

typedef enum gpu_engine_runtime_status {
    GPU_ENGINE_RUNTIME_OK = 0,
    GPU_ENGINE_RUNTIME_INVALID,
    GPU_ENGINE_RUNTIME_NOT_READY,
    GPU_ENGINE_RUNTIME_ALLOCATION_FAILED,
    GPU_ENGINE_RUNTIME_DEVICE_ERROR,
    GPU_ENGINE_RUNTIME_SYNC_ERROR,
    GPU_ENGINE_RUNTIME_RING_ERROR
} gpu_engine_runtime_status_t;

typedef int (*gpu_engine_runtime_quiesce_fn)(void *context);

typedef struct gpu_engine_runtime {
    gpu_dma_buffer_t dma;
    gpu_dma_sync_t sync;
    gpu_engine_ring_t ring;
    uint8_t initialized;
    uint8_t failed;
} gpu_engine_runtime_t;

/* Caller provides a zero-initialized runtime. Ring storage is coherent DMA memory. */
gpu_engine_runtime_status_t gpu_engine_runtime_init(
    gpu_engine_runtime_t *runtime, size_t ring_bytes, unsigned address_bits);
gpu_engine_runtime_status_t gpu_engine_runtime_submit(
    gpu_engine_runtime_t *runtime, const uint32_t *words, size_t word_count,
    gpu_engine_validate_fn validate, gpu_engine_publish_fn publish,
    void *publish_context);
gpu_engine_runtime_status_t gpu_engine_runtime_update_consumer(
    gpu_engine_runtime_t *runtime, uint64_t read_sequence);
gpu_engine_runtime_status_t gpu_engine_runtime_issue_fence(
    gpu_engine_runtime_t *runtime, uint64_t *fence);
gpu_engine_runtime_status_t gpu_engine_runtime_complete_fence(
    gpu_engine_runtime_t *runtime, uint64_t fence);
/* Never frees DMA memory unless the backend confirms that the device is quiesced. */
gpu_engine_runtime_status_t gpu_engine_runtime_destroy(
    gpu_engine_runtime_t *runtime, gpu_engine_runtime_quiesce_fn quiesce,
    void *context);

#endif
