/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_engine.h"

#include <limits.h>
#include <string.h>

static size_t gpu_engine_index(const gpu_engine_ring_t *ring,
                               uint64_t sequence)
{
    return (size_t)(sequence % (uint64_t)ring->capacity);
}

static uint64_t gpu_engine_queued(const gpu_engine_ring_t *ring)
{
    return ring->write_sequence - ring->read_sequence;
}

gpu_engine_status_t gpu_engine_ring_init(gpu_engine_ring_t *ring,
                                         uint32_t *storage, size_t capacity)
{
    if (ring == NULL || storage == NULL || capacity < 4U ||
        capacity > (size_t)(UINT64_MAX / 2U))
        return GPU_ENGINE_INVALID;
    memset(ring, 0, sizeof(*ring));
    ring->words = storage;
    ring->capacity = capacity;
    ring->next_fence = 1U;
    ring->initialized = 1U;
    return GPU_ENGINE_OK;
}

gpu_engine_status_t gpu_engine_ring_enable(gpu_engine_ring_t *ring)
{
    if (ring == NULL || ring->initialized == 0U || ring->words == NULL ||
        ring->capacity < 4U)
        return GPU_ENGINE_INVALID;
    if (ring->write_sequence != ring->read_sequence)
        return GPU_ENGINE_NOT_READY;
    ring->submission_enabled = 1U;
    return GPU_ENGINE_OK;
}

void gpu_engine_ring_disable(gpu_engine_ring_t *ring)
{
    if (ring != NULL)
        ring->submission_enabled = 0U;
}

gpu_engine_status_t gpu_engine_reserve(gpu_engine_ring_t *ring,
                                       size_t words,
                                       gpu_engine_transaction_t *transaction)
{
    uint64_t queued;

    if (transaction == NULL)
        return GPU_ENGINE_INVALID;
    memset(transaction, 0, sizeof(*transaction));
    if (ring == NULL || ring->initialized == 0U || ring->words == NULL ||
        ring->capacity < 4U)
        return GPU_ENGINE_INVALID;
    if (ring->submission_enabled == 0U)
        return GPU_ENGINE_NOT_READY;
    if (words == 0U || words >= ring->capacity)
        return GPU_ENGINE_BAD_PACKET;
    queued = gpu_engine_queued(ring);
    if (queued > ring->capacity)
        return GPU_ENGINE_INVALID;
    if ((uint64_t)words > (uint64_t)ring->capacity - queued)
        return GPU_ENGINE_FULL;
    transaction->ring = ring;
    transaction->start_sequence = ring->write_sequence;
    transaction->reserved_words = words;
    transaction->active = 1U;
    return GPU_ENGINE_OK;
}

gpu_engine_status_t gpu_engine_emit(gpu_engine_transaction_t *transaction,
                                    uint32_t word)
{
    gpu_engine_ring_t *ring;

    if (transaction == NULL || transaction->active == 0U ||
        transaction->ring == NULL)
        return GPU_ENGINE_INVALID;
    ring = transaction->ring;
    if (transaction->written_words >= transaction->reserved_words)
        return GPU_ENGINE_BAD_PACKET;
    ring->words[gpu_engine_index(ring, transaction->start_sequence +
                                transaction->written_words)] = word;
    ++transaction->written_words;
    return GPU_ENGINE_OK;
}

gpu_engine_status_t gpu_engine_commit(gpu_engine_transaction_t *transaction)
{
    gpu_engine_ring_t *ring;

    if (transaction == NULL || transaction->active == 0U ||
        transaction->ring == NULL)
        return GPU_ENGINE_INVALID;
    ring = transaction->ring;
    if (transaction->written_words != transaction->reserved_words ||
        ring->write_sequence != transaction->start_sequence)
        return GPU_ENGINE_BAD_PACKET;
    if (ring->write_sequence > UINT64_MAX - transaction->written_words)
        return GPU_ENGINE_INVALID;
    /* Publish only complete packets; architecture backend rings a doorbell later. */
    ring->write_sequence += transaction->written_words;
    transaction->active = 0U;
    return GPU_ENGINE_OK;
}

gpu_engine_status_t gpu_engine_submit(gpu_engine_transaction_t *transaction,
                                      gpu_engine_publish_fn publish,
                                      void *context)
{
    gpu_engine_ring_t *ring;
    uint64_t new_sequence;

    if (transaction == NULL || transaction->active == 0U ||
        transaction->ring == NULL || publish == NULL)
        return GPU_ENGINE_INVALID;
    ring = transaction->ring;
    if (transaction->written_words != transaction->reserved_words ||
        ring->write_sequence != transaction->start_sequence)
        return GPU_ENGINE_BAD_PACKET;
    if (ring->write_sequence > UINT64_MAX - transaction->written_words)
        return GPU_ENGINE_INVALID;
    new_sequence = ring->write_sequence + transaction->written_words;

    /* The backend must order packet memory before publishing its hardware tail. */
    if (!publish(context, new_sequence)) {
        ring->submission_enabled = 0U;
        transaction->active = 0U;
        return GPU_ENGINE_DEVICE_ERROR;
    }
    ring->write_sequence = new_sequence;
    transaction->active = 0U;
    return GPU_ENGINE_OK;
}

void gpu_engine_abort(gpu_engine_transaction_t *transaction)
{
    if (transaction != NULL)
        memset(transaction, 0, sizeof(*transaction));
}

gpu_engine_status_t gpu_engine_consume(gpu_engine_ring_t *ring,
                                       size_t max_words, size_t *consumed)
{
    uint64_t queued;
    size_t amount;

    if (consumed != NULL)
        *consumed = 0U;
    if (ring == NULL || ring->initialized == 0U || consumed == NULL)
        return GPU_ENGINE_INVALID;
    queued = gpu_engine_queued(ring);
    if (queued > ring->capacity)
        return GPU_ENGINE_INVALID;
    amount = queued < (uint64_t)max_words ? (size_t)queued : max_words;
    ring->read_sequence += amount;
    *consumed = amount;
    return GPU_ENGINE_OK;
}

gpu_engine_status_t gpu_engine_issue_fence(gpu_engine_ring_t *ring,
                                           uint64_t *fence)
{
    if (ring == NULL || fence == NULL || ring->initialized == 0U)
        return GPU_ENGINE_INVALID;
    if (ring->submission_enabled == 0U)
        return GPU_ENGINE_NOT_READY;
    if (ring->next_fence == 0U || ring->next_fence == UINT64_MAX)
        return GPU_ENGINE_NO_FENCE;
    ring->submitted_fence = ring->next_fence++;
    *fence = ring->submitted_fence;
    return GPU_ENGINE_OK;
}

gpu_engine_status_t gpu_engine_complete_fence(gpu_engine_ring_t *ring,
                                              uint64_t fence)
{
    if (ring == NULL || ring->initialized == 0U || fence == 0U)
        return GPU_ENGINE_INVALID;
    if (fence > ring->submitted_fence)
        return GPU_ENGINE_STALE_FENCE;
    if (fence < ring->completed_fence)
        return GPU_ENGINE_STALE_FENCE;
    ring->completed_fence = fence;
    return GPU_ENGINE_OK;
}

gpu_engine_status_t gpu_engine_snapshot(const gpu_engine_ring_t *ring,
                                        gpu_engine_snapshot_t *snapshot)
{
    uint64_t queued;

    if (ring == NULL || snapshot == NULL || ring->initialized == 0U)
        return GPU_ENGINE_INVALID;
    queued = gpu_engine_queued(ring);
    if (queued > ring->capacity)
        return GPU_ENGINE_INVALID;
    snapshot->capacity_words = ring->capacity;
    snapshot->queued_words = (size_t)queued;
    snapshot->free_words = ring->capacity - (size_t)queued;
    snapshot->submitted_fence = ring->submitted_fence;
    snapshot->completed_fence = ring->completed_fence;
    snapshot->initialized = ring->initialized;
    snapshot->submission_enabled = ring->submission_enabled;
    return GPU_ENGINE_OK;
}

gpu_engine_status_t gpu_engine_reset(gpu_engine_ring_t *ring)
{
    if (ring == NULL || ring->initialized == 0U)
        return GPU_ENGINE_INVALID;
    if (ring->submission_enabled != 0U ||
        ring->write_sequence != ring->read_sequence)
        return GPU_ENGINE_NOT_READY;
    ring->write_sequence = 0U;
    ring->read_sequence = 0U;
    ring->next_fence = 1U;
    ring->submitted_fence = 0U;
    ring->completed_fence = 0U;
    memset(ring->words, 0, ring->capacity * sizeof(ring->words[0]));
    return GPU_ENGINE_OK;
}
