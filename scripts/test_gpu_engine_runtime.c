/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_engine_runtime.h"
#include "heap.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint8_t memory[16384] __attribute__((aligned(4096)));
static unsigned frees;
static int fail_publish;
static int fail_quiesce;
static uint64_t published;

int heap_is_initialized(void) { return 1; }
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr) { free(ptr); }
int dma_alloc_contiguous(dma_page_t *pages, size_t count)
{
    if (pages == NULL || count == 0U || count > 4U) return 0;
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = memory + i * 4096U;
        pages[i].physical_address = 0x400000U + i * 4096U;
        pages[i].size = 4096U;
    }
    memset(memory, 0, count * 4096U);
    return 1;
}
int dma_page_is_valid(const dma_page_t *page)
{
    return page != NULL && page->virtual_address != NULL &&
        page->size == 4096U &&
        (((uintptr_t)page->virtual_address & 4095U) == 0U);
}
void dma_free_contiguous(dma_page_t *pages, size_t count)
{
    assert(pages != NULL && count > 0U);
    ++frees;
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = NULL;
        pages[i].physical_address = 0U;
        pages[i].size = 0U;
    }
}

static int validate(const gpu_engine_transaction_t *tx)
{
    return tx != NULL && tx->written_words != 0U &&
           tx->ring->words[tx->start_sequence % tx->ring->capacity] == 0x1234U;
}
static int publish(void *context, uint64_t sequence)
{
    (void)context;
    published = sequence;
    return !fail_publish;
}
static int quiesce(void *context)
{
    (void)context;
    return !fail_quiesce;
}

int main(void)
{
    gpu_engine_runtime_t runtime = {0};
    uint32_t words[] = { 0x1234U, 0x5678U };
    uint64_t fence = 0U;

    assert(gpu_engine_runtime_init(&runtime, 4096U, 32U) ==
           GPU_ENGINE_RUNTIME_OK);
    assert(runtime.initialized && runtime.dma.acquired);
    assert(gpu_engine_runtime_submit(&runtime, words, 2U, validate, publish,
                                     NULL) == GPU_ENGINE_RUNTIME_OK);
    assert(published == 2U);
    assert(gpu_engine_runtime_destroy(&runtime, quiesce, NULL) ==
           GPU_ENGINE_RUNTIME_NOT_READY);
    assert(gpu_engine_runtime_update_consumer(&runtime, 2U) ==
           GPU_ENGINE_RUNTIME_OK);
    assert(gpu_engine_runtime_issue_fence(&runtime, &fence) ==
           GPU_ENGINE_RUNTIME_OK);
    assert(gpu_engine_runtime_complete_fence(&runtime, fence) ==
           GPU_ENGINE_RUNTIME_OK);
    fail_quiesce = 1;
    assert(gpu_engine_runtime_destroy(&runtime, quiesce, NULL) ==
           GPU_ENGINE_RUNTIME_DEVICE_ERROR);
    assert(runtime.initialized && runtime.dma.acquired && frees == 0U);
    fail_quiesce = 0;
    /* Failed runtime stays closed: do not release memory without a proven reset. */
    assert(gpu_engine_runtime_destroy(&runtime, quiesce, NULL) ==
           GPU_ENGINE_RUNTIME_OK);
    assert(!runtime.initialized && frees == 1U);

    assert(gpu_engine_runtime_init(&runtime, 4096U, 32U) ==
           GPU_ENGINE_RUNTIME_OK);
    fail_publish = 1;
    assert(gpu_engine_runtime_submit(&runtime, words, 2U, validate, publish,
                                     NULL) == GPU_ENGINE_RUNTIME_DEVICE_ERROR);
    assert(gpu_engine_runtime_submit(&runtime, words, 2U, validate, publish,
                                     NULL) == GPU_ENGINE_RUNTIME_INVALID);
    fail_publish = 0;
    /* Device failure means teardown must prove the backend has quiesced. */
    assert(gpu_engine_runtime_update_consumer(&runtime, 2U) ==
           GPU_ENGINE_RUNTIME_NOT_READY);
    assert(gpu_engine_runtime_destroy(&runtime, quiesce, NULL) ==
           GPU_ENGINE_RUNTIME_OK);
    assert(!runtime.initialized && frees == 2U);
    return 0;
}
