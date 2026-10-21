/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_msgq_ga10x.h"
#include <string.h>

static int overlaps(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (!an || !bn) return 0;
    if (!a || !b || x > UINTPTR_MAX - an || y > UINTPTR_MAX - bn) return 1;
    return x < y + bn && y < x + an;
}

static int layout_matches_buffer(const nvx_dma_buffer_t *buffer,
    const nvx_gsp_modern_shared_queue_layout_t *layout)
{
    if (!buffer || !layout || !buffer->acquired || !buffer->cpu_address ||
        !buffer->gpu_address || !buffer->size ||
        buffer->allocated_size < buffer->size ||
        layout->shared_mem_phys_addr != buffer->gpu_address ||
        layout->allocation_size > buffer->size ||
        nvx_gsp_modern_shared_queue_validate(layout) != NVX_GSP_INIT_ARGS_OK)
        return 0;
    return 1;
}

nvx_gsp_msgq_ga10x_status_t nvx_gsp_msgq_ga10x_prepare_command(
    nvx_gsp_msgq_ga10x_t *runtime, const nvx_dma_buffer_t *shared_memory,
    const nvx_gsp_modern_shared_queue_layout_t *layout,
    const nvx_mmio_region_t *bar0)
{
    nvx_gsp_msgq_queue_status_t status;
    if (!runtime || !shared_memory || !layout || !bar0 || !bar0->base ||
        !bar0->length || !layout_matches_buffer(shared_memory, layout) ||
        overlaps(runtime, sizeof(*runtime), shared_memory->cpu_address,
                 shared_memory->allocated_size) ||
        overlaps(runtime, sizeof(*runtime), bar0, sizeof(*bar0)) ||
        overlaps(runtime, sizeof(*runtime), (const void *)bar0->base,
                 bar0->length))
        return NVX_GSP_MSGQ_GA10X_INVALID_ARGUMENT;
    if (runtime->command_initialized || runtime->status_linked)
        return NVX_GSP_MSGQ_GA10X_INVALID_ARGUMENT;
    if (bar0->length < NVX_GSP_GA10X_MSGQ_TAIL(NVX_GSP_GA10X_RM_QUEUE_INDEX) + 4U)
        return NVX_GSP_MSGQ_GA10X_IO_ERROR;

    memset(runtime, 0, sizeof(*runtime));
    if (nvx_gsp_msgq_io_init_mmio(&runtime->io, &runtime->mmio_context,
                                  bar0) != NVX_GSP_MSGQ_QUEUE_OK)
        return NVX_GSP_MSGQ_GA10X_IO_ERROR;

    status = nvx_gsp_msgq_queue_init_tx(
        &runtime->command_queue,
        (uint8_t *)shared_memory->cpu_address + layout->command_queue_offset,
        (size_t)layout->command_queue_size, NVX_GSP_PAGE_SIZE, &runtime->io,
        NVX_GSP_GA10X_QUEUE_HEAD(NVX_GSP_GA10X_RM_QUEUE_INDEX),
        NVX_GSP_GA10X_QUEUE_TAIL(NVX_GSP_GA10X_RM_QUEUE_INDEX));
    if (status != NVX_GSP_MSGQ_QUEUE_OK)
        return status == NVX_GSP_MSGQ_QUEUE_IO_ERROR
            ? NVX_GSP_MSGQ_GA10X_IO_ERROR : NVX_GSP_MSGQ_GA10X_BAD_LAYOUT;
    runtime->command_initialized = 1U;
    return NVX_GSP_MSGQ_GA10X_OK;
}

nvx_gsp_msgq_ga10x_status_t nvx_gsp_msgq_ga10x_link_status(
    nvx_gsp_msgq_ga10x_t *runtime, const nvx_dma_buffer_t *shared_memory,
    const nvx_dma_sync_t *shared_sync,
    const nvx_gsp_modern_shared_queue_layout_t *layout,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_msgq_ga10x_delay_fn delay_ms, void *context)
{
    uint32_t elapsed = 0U;
    nvx_gsp_msgq_queue_status_t status;
    if (!runtime || !shared_memory || !shared_sync || !layout || !delay_ms ||
        !runtime->command_initialized || runtime->status_linked ||
        timeout_ms == 0U || poll_interval_ms == 0U ||
        !layout_matches_buffer(shared_memory, layout) ||
        !shared_sync->initialized || shared_sync->buffer != shared_memory ||
        overlaps(runtime, sizeof(*runtime), shared_sync, sizeof(*shared_sync)))
        return NVX_GSP_MSGQ_GA10X_INVALID_ARGUMENT;
    if (shared_sync->owner != NVX_DMA_OWNER_SHARED)
        return NVX_GSP_MSGQ_GA10X_NOT_SHARED;

    do {
        status = nvx_gsp_msgq_queue_init_rx(
            &runtime->status_queue,
            (uint8_t *)shared_memory->cpu_address + layout->status_queue_offset,
            (size_t)layout->status_queue_size, NVX_GSP_PAGE_SIZE, &runtime->io,
            NVX_GSP_GA10X_MSGQ_HEAD(NVX_GSP_GA10X_RM_QUEUE_INDEX),
            NVX_GSP_GA10X_MSGQ_TAIL(NVX_GSP_GA10X_RM_QUEUE_INDEX));
        if (status == NVX_GSP_MSGQ_QUEUE_OK) {
            runtime->status_linked = 1U;
            return NVX_GSP_MSGQ_GA10X_OK;
        }
        if (status != NVX_GSP_MSGQ_QUEUE_BAD_LAYOUT)
            return status == NVX_GSP_MSGQ_QUEUE_IO_ERROR
                ? NVX_GSP_MSGQ_GA10X_IO_ERROR : NVX_GSP_MSGQ_GA10X_BAD_LAYOUT;

        uint32_t delay = poll_interval_ms;
        if (delay > timeout_ms - elapsed) delay = timeout_ms - elapsed;
        delay_ms(delay, context);
        elapsed += delay;
    } while (elapsed < timeout_ms);
    return NVX_GSP_MSGQ_GA10X_TIMEOUT;
}
