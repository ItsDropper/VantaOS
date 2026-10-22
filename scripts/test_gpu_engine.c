/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_engine.h"

#include <assert.h>
#include <stdint.h>

static int publish_ok(void *context, uint64_t sequence)
{
    uint64_t *observed = (uint64_t *)context;
    *observed = sequence;
    return 1;
}

static int publish_fail(void *context, uint64_t sequence)
{
    (void)context;
    (void)sequence;
    return 0;
}

int main(void)
{
    uint32_t storage[8] = {0U};
    gpu_engine_ring_t ring;
    gpu_engine_transaction_t tx;
    gpu_engine_snapshot_t snap;
    size_t consumed = 0U;
    uint64_t fence = 0U, published_sequence = 0U;

    assert(gpu_engine_ring_init(NULL, storage, 8U) == GPU_ENGINE_INVALID);
    assert(gpu_engine_ring_init(&ring, storage, 3U) == GPU_ENGINE_INVALID);
    assert(gpu_engine_ring_init(&ring, storage, 8U) == GPU_ENGINE_OK);
    assert(gpu_engine_reserve(&ring, 2U, &tx) == GPU_ENGINE_NOT_READY);
    assert(gpu_engine_ring_enable(&ring) == GPU_ENGINE_OK);
    assert(gpu_engine_reserve(&ring, 3U, &tx) == GPU_ENGINE_OK);
    assert(gpu_engine_emit(&tx, 0xDEADBEEFU) == GPU_ENGINE_OK);
    assert(gpu_engine_emit(&tx, 0x00000001U) == GPU_ENGINE_OK);
    assert(gpu_engine_commit(&tx) == GPU_ENGINE_BAD_PACKET);
    gpu_engine_abort(&tx);
    assert(gpu_engine_reserve(&ring, 3U, &tx) == GPU_ENGINE_OK);
    assert(gpu_engine_emit(&tx, 0x10000000U) == GPU_ENGINE_OK);
    assert(gpu_engine_emit(&tx, 0x20000000U) == GPU_ENGINE_OK);
    assert(gpu_engine_emit(&tx, 0x30000000U) == GPU_ENGINE_OK);
    assert(gpu_engine_commit(&tx) == GPU_ENGINE_OK);
    assert(gpu_engine_reserve(&ring, 6U, &tx) == GPU_ENGINE_FULL);
    assert(gpu_engine_snapshot(&ring, &snap) == GPU_ENGINE_OK);
    assert(snap.queued_words == 3U && snap.free_words == 5U);
    assert(storage[0] == 0x10000000U && storage[2] == 0x30000000U);
    assert(gpu_engine_consume(&ring, 2U, &consumed) == GPU_ENGINE_OK);
    assert(consumed == 2U);
    assert(gpu_engine_reserve(&ring, 4U, &tx) == GPU_ENGINE_OK);
    assert(gpu_engine_emit(&tx, 0x40000000U) == GPU_ENGINE_OK);
    assert(gpu_engine_emit(&tx, 0x50000000U) == GPU_ENGINE_OK);
    assert(gpu_engine_emit(&tx, 0x60000000U) == GPU_ENGINE_OK);
    assert(gpu_engine_emit(&tx, 0x70000000U) == GPU_ENGINE_OK);
    assert(gpu_engine_commit(&tx) == GPU_ENGINE_OK);
    assert(storage[3] == 0x40000000U && storage[6] == 0x70000000U);
    assert(gpu_engine_issue_fence(&ring, &fence) == GPU_ENGINE_OK);
    assert(fence == 1U);
    assert(gpu_engine_complete_fence(&ring, 2U) == GPU_ENGINE_STALE_FENCE);
    assert(gpu_engine_complete_fence(&ring, fence) == GPU_ENGINE_OK);
    assert(gpu_engine_complete_fence(&ring, fence - 1U) == GPU_ENGINE_INVALID);
    gpu_engine_ring_disable(&ring);
    assert(gpu_engine_reset(&ring) == GPU_ENGINE_NOT_READY);
    assert(gpu_engine_consume(&ring, 99U, &consumed) == GPU_ENGINE_OK);
    assert(consumed == 5U);
    assert(gpu_engine_reset(&ring) == GPU_ENGINE_OK);
    assert(gpu_engine_snapshot(&ring, &snap) == GPU_ENGINE_OK);
    assert(snap.queued_words == 0U && snap.submitted_fence == 0U);
    assert(gpu_engine_ring_enable(&ring) == GPU_ENGINE_OK);
    assert(gpu_engine_issue_fence(&ring, &fence) == GPU_ENGINE_OK);
    assert(gpu_engine_reserve(&ring, 1U, &tx) == GPU_ENGINE_OK);
    assert(gpu_engine_emit(&tx, 0xABCDEF01U) == GPU_ENGINE_OK);
    assert(gpu_engine_submit(&tx, publish_ok, &published_sequence) == GPU_ENGINE_OK);
    assert(published_sequence == 1U);
    assert(gpu_engine_reserve(&ring, 1U, &tx) == GPU_ENGINE_OK);
    assert(gpu_engine_emit(&tx, 0xABCDEF02U) == GPU_ENGINE_OK);
    assert(gpu_engine_submit(&tx, publish_fail, NULL) == GPU_ENGINE_DEVICE_ERROR);
    assert(ring.submission_enabled == 0U);
    assert(ring.write_sequence == 1U);
    return 0;
}
