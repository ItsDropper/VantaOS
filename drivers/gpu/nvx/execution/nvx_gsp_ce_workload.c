/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_ce_workload.h"
#include <string.h>

#define OBJECT_COUNT 4U
#define COMMAND_INDEX 0U
#define SOURCE_INDEX 1U
#define DESTINATION_INDEX 2U
#define COMPLETION_INDEX 3U

static int same_handle(const nvx_memory_handle_t *a,
                       const nvx_memory_handle_t *b)
{
    return a->token == b->token && a->slot == b->slot &&
        a->generation == b->generation &&
        a->gpu_virtual_address == b->gpu_virtual_address &&
        a->size == b->size;
}

static nvx_gsp_ce_workload_status_t restore_cpu_ownership(
    nvx_gsp_ce_workload_t *w)
{
    for (size_t i = 0U; i < OBJECT_COUNT; ++i) {
        nvx_dma_sync_t *sync = &w->sync[i];
        if (sync->initialized && sync->owner != NVX_DMA_OWNER_CPU &&
            nvx_dma_sync_for_cpu(sync, 0U, sync->size) != NVX_DMA_SYNC_OK)
            return NVX_GSP_CE_WORKLOAD_SYNC_ERROR;
    }
    return NVX_GSP_CE_WORKLOAD_OK;
}

static nvx_gsp_ce_workload_status_t release_resources(
    nvx_gsp_ce_workload_t *w)
{
    nvx_gsp_ce_workload_status_t status = restore_cpu_ownership(w);
    if (status != NVX_GSP_CE_WORKLOAD_OK) {
        w->stage = NVX_GSP_CE_WORKLOAD_CLEANUP_PENDING;
        return status;
    }
    for (size_t i = 0U; i < OBJECT_COUNT; ++i) {
        if (w->sync[i].initialized &&
            nvx_dma_sync_destroy(&w->sync[i]) != NVX_DMA_SYNC_OK) {
            w->stage = NVX_GSP_CE_WORKLOAD_CLEANUP_PENDING;
            return NVX_GSP_CE_WORKLOAD_SYNC_ERROR;
        }
    }
    while (w->pinned_count != 0U) {
        uint32_t index = w->pinned_count - 1U;
        if (nvx_memory_unpin(w->memory, &w->handles[index]) != NVX_MEMORY_OK) {
            w->stage = NVX_GSP_CE_WORKLOAD_CLEANUP_PENDING;
            return NVX_GSP_CE_WORKLOAD_MEMORY_ERROR;
        }
        w->pinned_count = index;
    }
    memset(w, 0, sizeof(*w));
    return NVX_GSP_CE_WORKLOAD_OK;
}

static nvx_gsp_ce_workload_status_t initialize_objects(
    nvx_gsp_ce_workload_t *w, nvx_memory_manager_t *memory,
    const nvx_memory_handle_t *const handles[OBJECT_COUNT],
    nvx_memory_info_t info[OBJECT_COUNT])
{
    for (size_t i = 0U; i < OBJECT_COUNT; ++i) {
        if (!handles[i] || !handles[i]->token)
            return NVX_GSP_CE_WORKLOAD_INVALID_ARGUMENT;
        for (size_t j = 0U; j < i; ++j)
            if (same_handle(handles[i], handles[j]))
                return NVX_GSP_CE_WORKLOAD_INVALID_ARGUMENT;
        if (nvx_memory_get_info(memory, handles[i], &info[i]) != NVX_MEMORY_OK ||
            !info[i].cpu_address || !info[i].size)
            return NVX_GSP_CE_WORKLOAD_MEMORY_ERROR;
    }
    if (info[COMMAND_INDEX].kind != NVX_MEMORY_COMMAND_STORAGE ||
        (info[COMMAND_INDEX].access & NVX_MEMORY_ACCESS_READ) == 0U ||
        (info[SOURCE_INDEX].access & NVX_MEMORY_ACCESS_READ) == 0U ||
        (info[DESTINATION_INDEX].access & NVX_MEMORY_ACCESS_WRITE) == 0U ||
        (info[COMPLETION_INDEX].access & NVX_MEMORY_ACCESS_WRITE) == 0U ||
        info[COMMAND_INDEX].size < NVX_GSP_CE_COPY_COMMAND_BYTES ||
        info[COMPLETION_INDEX].size < sizeof(uint32_t))
        return NVX_GSP_CE_WORKLOAD_INVALID_ARGUMENT;

    w->memory = memory;
    for (size_t i = 0U; i < OBJECT_COUNT; ++i) {
        nvx_dma_buffer_t *buffer = 0;
        w->handles[i] = *handles[i];
        if (nvx_memory_pin(memory, handles[i]) != NVX_MEMORY_OK)
            return NVX_GSP_CE_WORKLOAD_MEMORY_ERROR;
        ++w->pinned_count;
        if (nvx_memory_get_dma_buffer(memory, handles[i], &buffer) !=
                NVX_MEMORY_OK ||
            nvx_dma_sync_init(&w->sync[i], buffer) != NVX_DMA_SYNC_OK)
            return NVX_GSP_CE_WORKLOAD_MEMORY_ERROR;
    }
    return NVX_GSP_CE_WORKLOAD_OK;
}

static nvx_gsp_ce_workload_status_t finish_completed(
    nvx_gsp_ce_workload_t *w)
{
    nvx_gsp_ce_workload_status_t status = restore_cpu_ownership(w);
    if (status != NVX_GSP_CE_WORKLOAD_OK) {
        w->stage = NVX_GSP_CE_WORKLOAD_RELEASE_PENDING;
        return status;
    }
    if (!w->completion_word ||
        __atomic_load_n(w->completion_word, __ATOMIC_ACQUIRE) !=
            w->completion_value) {
        w->stage = NVX_GSP_CE_WORKLOAD_RELEASE_PENDING;
        return NVX_GSP_CE_WORKLOAD_COMPLETION_ERROR;
    }
    w->stage = NVX_GSP_CE_WORKLOAD_RELEASE_PENDING;
    return release_resources(w);
}

nvx_gsp_ce_workload_status_t nvx_gsp_ce_copy_start(
    nvx_gsp_ce_workload_t *w, nvx_memory_manager_t *memory,
    nvx_gsp_gpfifo_channel_t *channel,
    const nvx_memory_handle_t *command,
    const nvx_memory_handle_t *source,
    const nvx_memory_handle_t *destination,
    const nvx_memory_handle_t *completion,
    uint32_t copy_class, uint32_t subchannel, uint32_t bytes,
    uint32_t completion_value, uint32_t timeout_ms,
    uint32_t poll_interval_ms, nvx_gsp_gpfifo_delay_fn delay_ms,
    void *delay_context, nvx_gsp_gpfifo_status_t *out_fifo_status)
{
    const nvx_memory_handle_t *handles[OBJECT_COUNT] = {
        command, source, destination, completion
    };
    nvx_memory_info_t info[OBJECT_COUNT] = {{0}};
    nvx_gsp_ce_workload_status_t status;
    nvx_gsp_gpfifo_status_t fifo_status;
    uint8_t submitted = 0U;
    size_t command_bytes = 0U;

    if (out_fifo_status)
        *out_fifo_status = NVX_GSP_GPFIFO_INVALID_ARGUMENT;
    if (!w || !memory || !channel || !command || !source ||
        !destination || !completion || !timeout_ms || !poll_interval_ms ||
        !delay_ms || w->stage != NVX_GSP_CE_WORKLOAD_IDLE ||
        w->pinned_count != 0U)
        return NVX_GSP_CE_WORKLOAD_INVALID_ARGUMENT;

    memset(w, 0, sizeof(*w));
    status = initialize_objects(w, memory, handles, info);
    if (status != NVX_GSP_CE_WORKLOAD_OK) {
        nvx_gsp_ce_workload_status_t cleanup = release_resources(w);
        return cleanup == NVX_GSP_CE_WORKLOAD_OK ? status : cleanup;
    }

    if (!bytes || bytes > info[SOURCE_INDEX].size ||
        bytes > info[DESTINATION_INDEX].size ||
        info[COMMAND_INDEX].size > SIZE_MAX ||
        info[COMPLETION_INDEX].size < sizeof(uint32_t) ||
        ((uintptr_t)info[COMPLETION_INDEX].cpu_address & 3U) != 0U) {
        status = NVX_GSP_CE_WORKLOAD_INVALID_ARGUMENT;
        goto fail_before_submit;
    }

    w->completion_word = (volatile uint32_t *)info[COMPLETION_INDEX].cpu_address;
    w->completion_value = completion_value;
    __atomic_store_n(w->completion_word, completion_value ^ UINT32_MAX,
                     __ATOMIC_RELAXED);

    if (nvx_gsp_ce_build_copy(
        (volatile uint32_t *)info[COMMAND_INDEX].cpu_address,
        (size_t)info[COMMAND_INDEX].size, copy_class, subchannel,
        info[SOURCE_INDEX].gpu_virtual_address,
        info[DESTINATION_INDEX].gpu_virtual_address,
        info[COMPLETION_INDEX].gpu_virtual_address, bytes,
        completion_value, &command_bytes) != NVX_GSP_CE_OK) {
        status = NVX_GSP_CE_WORKLOAD_BUILD_ERROR;
        goto fail_before_submit;
    }

    for (size_t i = 0U; i < OBJECT_COUNT; ++i) {
        nvx_dma_sync_status_t sync_status;
        if (i == COMPLETION_INDEX) {
            /* CPU polls this GPU-written semaphore while the CE may update it. */
            sync_status = nvx_dma_sync_for_shared(&w->sync[i], 0U,
                w->sync[i].size);
        } else {
            sync_status = nvx_dma_sync_for_device(&w->sync[i], 0U,
                w->sync[i].size);
        }
        if (sync_status != NVX_DMA_SYNC_OK) {
            status = NVX_GSP_CE_WORKLOAD_SYNC_ERROR;
            goto fail_before_submit;
        }
    }

    fifo_status = nvx_gsp_gpfifo_submit_and_wait_ex(channel,
        info[COMMAND_INDEX].gpu_virtual_address, command_bytes,
        w->completion_word, completion_value, timeout_ms, poll_interval_ms,
        delay_ms, delay_context, &w->put_index, &submitted);
    if (out_fifo_status)
        *out_fifo_status = fifo_status;
    if (submitted && fifo_status != NVX_GSP_GPFIFO_OK) {
        w->stage = NVX_GSP_CE_WORKLOAD_PENDING;
        return NVX_GSP_CE_WORKLOAD_IN_FLIGHT;
    }
    if (fifo_status != NVX_GSP_GPFIFO_OK) {
        status = NVX_GSP_CE_WORKLOAD_SUBMIT_ERROR;
        goto fail_before_submit;
    }

    w->stage = NVX_GSP_CE_WORKLOAD_RELEASE_PENDING;
    return finish_completed(w);

fail_before_submit:
    {
        nvx_gsp_ce_workload_status_t cleanup = release_resources(w);
        return cleanup == NVX_GSP_CE_WORKLOAD_OK ? status : cleanup;
    }
}

nvx_gsp_ce_workload_status_t nvx_gsp_ce_copy_recover(
    nvx_gsp_ce_workload_t *w, uint32_t timeout_ms,
    uint32_t poll_interval_ms, nvx_gsp_gpfifo_delay_fn delay_ms,
    void *delay_context)
{
    nvx_gsp_gpfifo_status_t status;
    if (!w || !timeout_ms || !poll_interval_ms || !delay_ms || !w->memory)
        return NVX_GSP_CE_WORKLOAD_INVALID_ARGUMENT;

    if (w->stage == NVX_GSP_CE_WORKLOAD_CLEANUP_PENDING)
        return release_resources(w);
    if (w->stage == NVX_GSP_CE_WORKLOAD_PENDING) {
        status = nvx_gsp_gpfifo_wait_completion(w->completion_word,
            w->completion_value, timeout_ms, poll_interval_ms,
            delay_ms, delay_context);
        if (status == NVX_GSP_GPFIFO_TIMEOUT)
            return NVX_GSP_CE_WORKLOAD_TIMEOUT;
        if (status != NVX_GSP_GPFIFO_OK)
            return NVX_GSP_CE_WORKLOAD_COMPLETION_ERROR;
        w->stage = NVX_GSP_CE_WORKLOAD_RELEASE_PENDING;
    } else if (w->stage != NVX_GSP_CE_WORKLOAD_RELEASE_PENDING) {
        return NVX_GSP_CE_WORKLOAD_INVALID_ARGUMENT;
    }
    return finish_completed(w);
}
