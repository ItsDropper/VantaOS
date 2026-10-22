/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_ENGINE_H
#define VANTA_GPU_ENGINE_H

#include <stddef.h>
#include <stdint.h>

typedef enum gpu_engine_status {
    GPU_ENGINE_OK = 0,
    GPU_ENGINE_INVALID,
    GPU_ENGINE_NOT_READY,
    GPU_ENGINE_FULL,
    GPU_ENGINE_BAD_PACKET,
    GPU_ENGINE_NO_FENCE,
    GPU_ENGINE_STALE_FENCE
} gpu_engine_status_t;

typedef struct gpu_engine_ring {
    uint32_t *words;
    size_t capacity;
    uint64_t write_sequence;
    uint64_t read_sequence;
    uint64_t next_fence;
    uint64_t submitted_fence;
    uint64_t completed_fence;
    uint8_t initialized;
    uint8_t submission_enabled;
} gpu_engine_ring_t;

typedef struct gpu_engine_transaction {
    gpu_engine_ring_t *ring;
    uint64_t start_sequence;
    size_t reserved_words;
    size_t written_words;
    uint8_t active;
} gpu_engine_transaction_t;

typedef struct gpu_engine_snapshot {
    size_t capacity_words;
    size_t queued_words;
    size_t free_words;
    uint64_t submitted_fence;
    uint64_t completed_fence;
    uint8_t initialized;
    uint8_t submission_enabled;
} gpu_engine_snapshot_t;

gpu_engine_status_t gpu_engine_ring_init(gpu_engine_ring_t *ring,
                                         uint32_t *storage, size_t capacity);
gpu_engine_status_t gpu_engine_ring_enable(gpu_engine_ring_t *ring);
void gpu_engine_ring_disable(gpu_engine_ring_t *ring);
gpu_engine_status_t gpu_engine_reserve(gpu_engine_ring_t *ring,
                                       size_t words,
                                       gpu_engine_transaction_t *transaction);
gpu_engine_status_t gpu_engine_emit(gpu_engine_transaction_t *transaction,
                                    uint32_t word);
gpu_engine_status_t gpu_engine_commit(gpu_engine_transaction_t *transaction);
void gpu_engine_abort(gpu_engine_transaction_t *transaction);
gpu_engine_status_t gpu_engine_consume(gpu_engine_ring_t *ring,
                                       size_t max_words, size_t *consumed);
gpu_engine_status_t gpu_engine_issue_fence(gpu_engine_ring_t *ring,
                                           uint64_t *fence);
gpu_engine_status_t gpu_engine_complete_fence(gpu_engine_ring_t *ring,
                                              uint64_t fence);
gpu_engine_status_t gpu_engine_snapshot(const gpu_engine_ring_t *ring,
                                        gpu_engine_snapshot_t *snapshot);
gpu_engine_status_t gpu_engine_reset(gpu_engine_ring_t *ring);

#endif
