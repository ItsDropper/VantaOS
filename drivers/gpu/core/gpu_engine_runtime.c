/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_engine_runtime.h"

#include <string.h>

static gpu_engine_runtime_status_t gpu_engine_runtime_map_ring(
    gpu_engine_status_t status)
{
    if (status == GPU_ENGINE_OK)
        return GPU_ENGINE_RUNTIME_OK;
    if (status == GPU_ENGINE_NOT_READY)
        return GPU_ENGINE_RUNTIME_NOT_READY;
    if (status == GPU_ENGINE_DEVICE_ERROR)
        return GPU_ENGINE_RUNTIME_DEVICE_ERROR;
    if (status == GPU_ENGINE_INVALID)
        return GPU_ENGINE_RUNTIME_INVALID;
    return GPU_ENGINE_RUNTIME_RING_ERROR;
}

gpu_engine_runtime_status_t gpu_engine_runtime_init(
    gpu_engine_runtime_t *runtime, size_t ring_bytes, unsigned address_bits)
{
    gpu_dma_status_t dma_status;
    gpu_dma_sync_status_t sync_status;
    gpu_engine_status_t ring_status;

    if (runtime == NULL || runtime->initialized != 0U ||
        runtime->dma.acquired != 0U || ring_bytes < 16U ||
        (ring_bytes & (sizeof(uint32_t) - 1U)) != 0U)
        return GPU_ENGINE_RUNTIME_INVALID;

    memset(runtime, 0, sizeof(*runtime));
    dma_status = gpu_dma_alloc(ring_bytes, address_bits, &runtime->dma);
    if (dma_status != GPU_DMA_OK)
        return GPU_ENGINE_RUNTIME_ALLOCATION_FAILED;

    sync_status = gpu_dma_sync_init(&runtime->sync, &runtime->dma);
    if (sync_status != GPU_DMA_SYNC_OK)
        goto fail_dma;
    sync_status = gpu_dma_sync_for_shared(&runtime->sync, 0U,
                                          runtime->dma.size);
    if (sync_status != GPU_DMA_SYNC_OK)
        goto fail_sync;

    ring_status = gpu_engine_ring_init(
        &runtime->ring, (uint32_t *)runtime->dma.cpu_address,
        runtime->dma.allocated_size / sizeof(uint32_t));
    if (ring_status != GPU_ENGINE_OK)
        goto fail_sync;
    ring_status = gpu_engine_ring_enable(&runtime->ring);
    if (ring_status != GPU_ENGINE_OK)
        goto fail_sync;

    runtime->initialized = 1U;
    return GPU_ENGINE_RUNTIME_OK;

fail_sync:
    /* The allocation is still owned by the CPU if ring setup failed. */
    if (runtime->sync.initialized != 0U) {
        if (runtime->sync.owner == GPU_DMA_OWNER_SHARED)
            (void)gpu_dma_sync_for_cpu(&runtime->sync, 0U,
                                       runtime->dma.size);
        (void)gpu_dma_sync_destroy(&runtime->sync);
    }
fail_dma:
    gpu_dma_release(&runtime->dma);
    memset(runtime, 0, sizeof(*runtime));
    return GPU_ENGINE_RUNTIME_SYNC_ERROR;
}

gpu_engine_runtime_status_t gpu_engine_runtime_submit(
    gpu_engine_runtime_t *runtime, const uint32_t *words, size_t word_count,
    gpu_engine_validate_fn validate, gpu_engine_publish_fn publish,
    void *publish_context)
{
    gpu_engine_transaction_t transaction;
    gpu_engine_status_t status;

    if (runtime == NULL || runtime->initialized == 0U ||
        runtime->failed != 0U || words == NULL || word_count == 0U ||
        validate == NULL || publish == NULL)
        return GPU_ENGINE_RUNTIME_INVALID;

    status = gpu_engine_reserve(&runtime->ring, word_count, &transaction);
    if (status != GPU_ENGINE_OK)
        return gpu_engine_runtime_map_ring(status);
    for (size_t i = 0U; i < word_count; ++i) {
        status = gpu_engine_emit(&transaction, words[i]);
        if (status != GPU_ENGINE_OK) {
            gpu_engine_abort(&transaction);
            return gpu_engine_runtime_map_ring(status);
        }
    }

    status = gpu_engine_submit(&transaction, validate, publish, publish_context);
    if (status == GPU_ENGINE_BAD_PACKET)
        gpu_engine_abort(&transaction);
    else if (status == GPU_ENGINE_DEVICE_ERROR)
        runtime->failed = 1U;
    return gpu_engine_runtime_map_ring(status);
}

gpu_engine_runtime_status_t gpu_engine_runtime_update_consumer(
    gpu_engine_runtime_t *runtime, uint64_t read_sequence)
{
    if (runtime == NULL || runtime->initialized == 0U ||
        runtime->failed != 0U)
        return GPU_ENGINE_RUNTIME_NOT_READY;
    return gpu_engine_runtime_map_ring(
        gpu_engine_update_consumer(&runtime->ring, read_sequence));
}

gpu_engine_runtime_status_t gpu_engine_runtime_issue_fence(
    gpu_engine_runtime_t *runtime, uint64_t *fence)
{
    if (runtime == NULL || runtime->initialized == 0U ||
        runtime->failed != 0U)
        return GPU_ENGINE_RUNTIME_NOT_READY;
    return gpu_engine_runtime_map_ring(
        gpu_engine_issue_fence(&runtime->ring, fence));
}

gpu_engine_runtime_status_t gpu_engine_runtime_complete_fence(
    gpu_engine_runtime_t *runtime, uint64_t fence)
{
    if (runtime == NULL || runtime->initialized == 0U)
        return GPU_ENGINE_RUNTIME_NOT_READY;
    return gpu_engine_runtime_map_ring(
        gpu_engine_complete_fence(&runtime->ring, fence));
}

gpu_engine_runtime_status_t gpu_engine_runtime_destroy(
    gpu_engine_runtime_t *runtime, gpu_engine_runtime_quiesce_fn quiesce,
    void *context)
{
    gpu_dma_sync_status_t sync_status;

    if (runtime == NULL || runtime->initialized == 0U || quiesce == NULL)
        return GPU_ENGINE_RUNTIME_INVALID;
    if (runtime->ring.read_sequence != runtime->ring.write_sequence ||
        runtime->ring.completed_fence < runtime->ring.submitted_fence)
        return GPU_ENGINE_RUNTIME_NOT_READY;
    if (!quiesce(context)) {
        runtime->failed = 1U;
        gpu_engine_ring_disable(&runtime->ring);
        return GPU_ENGINE_RUNTIME_DEVICE_ERROR;
    }

    gpu_engine_ring_disable(&runtime->ring);
    sync_status = gpu_dma_sync_for_cpu(&runtime->sync, 0U, runtime->dma.size);
    if (sync_status != GPU_DMA_SYNC_OK)
        return GPU_ENGINE_RUNTIME_SYNC_ERROR;
    sync_status = gpu_dma_sync_destroy(&runtime->sync);
    if (sync_status != GPU_DMA_SYNC_OK)
        return GPU_ENGINE_RUNTIME_SYNC_ERROR;
    gpu_dma_release(&runtime->dma);
    memset(runtime, 0, sizeof(*runtime));
    return GPU_ENGINE_RUNTIME_OK;
}
