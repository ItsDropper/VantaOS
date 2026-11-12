/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_channel_ring.h"

#include <limits.h>
#include <string.h>

static int power_of_two(uint32_t value)
{
    return value >= 2U && (value & (value - 1U)) == 0U;
}

static void clear_ring(nvx_gsp_channel_ring_t *ring)
{
    memset(ring, 0, sizeof(*ring));
}

static nvx_gsp_rmapi_status_t begin_context_create(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_channel_ring_t *ring,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    status = nvx_gsp_context_dma_create(manager, &ring->context_dma,
        ring->client_handle, ring->device_handle, ring->context_dma_handle,
        ring->memory.memory_handle, ring->subdevice_handle, 0U, 0U,
        (uint64_t)ring->bytes - 1U, scratch, scratch_size, response,
        response_size, timeout_ms, poll_ms, delay_ms, delay_context);
    if (status == NVX_GSP_RMAPI_OK) {
        ring->stage = NVX_GSP_CHANNEL_RING_CONTEXT_CREATE_PENDING;
        /* The operation completed synchronously; continue with the map. */
    } else if (ring->context_dma.stage != NVX_GSP_CONTEXT_DMA_IDLE) {
        ring->stage = NVX_GSP_CHANNEL_RING_CONTEXT_CREATE_PENDING;
        return status;
    } else {
        ring->stage = NVX_GSP_CHANNEL_RING_MEMORY_DESTROY_PENDING;
        return status;
    }

    status = nvx_gsp_os_memory_dma_map(manager, &ring->memory,
        &ring->context_dma, &ring->mapping, 0U, (uint64_t)ring->bytes,
        0U, 0U, 0U, 0U, scratch, scratch_size, response, response_size,
        timeout_ms, poll_ms, delay_ms, delay_context);
    if (status != NVX_GSP_RMAPI_OK) {
        if (ring->mapping.stage != NVX_GSP_DMA_MAP_IDLE)
            ring->stage = NVX_GSP_CHANNEL_RING_MAP_PENDING;
        else
            ring->stage = NVX_GSP_CHANNEL_RING_CONTEXT_DESTROY_PENDING;
        return status;
    }

    ring->gpu_virtual_address = ring->mapping.dma_offset;
    if ((ring->gpu_virtual_address & 7U) != 0U ||
        ring->gpu_virtual_address >= NVX_GSP_USERD_GPUVA_LIMIT ||
        (uint64_t)ring->bytes >
            NVX_GSP_USERD_GPUVA_LIMIT - ring->gpu_virtual_address) {
        ring->stage = NVX_GSP_CHANNEL_RING_UNMAP_PENDING;
        return NVX_GSP_RMAPI_MALFORMED_RESPONSE;
    }

    ring->entries = (volatile uint32_t *)ring->memory.backing.cpu_address;
    if (!ring->entries || ((uintptr_t)ring->entries & 7U) != 0U) {
        ring->stage = NVX_GSP_CHANNEL_RING_UNMAP_PENDING;
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    }
    memset((void *)ring->entries, 0, ring->bytes);
    ring->stage = NVX_GSP_CHANNEL_RING_READY;
    return NVX_GSP_RMAPI_OK;
}

static nvx_gsp_rmapi_status_t finish_memory_destroy(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_channel_ring_t *ring,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    if (ring->memory_lease_held) {
        status = nvx_gsp_os_memory_release(&ring->memory);
        if (status != NVX_GSP_RMAPI_OK)
            return NVX_GSP_RMAPI_CLEANUP_FAILED;
        ring->memory_lease_held = 0U;
    }
    status = nvx_gsp_os_memory_destroy(manager, &ring->memory, scratch,
        scratch_size, response, response_size, timeout_ms, poll_ms,
        delay_ms, delay_context);
    if (status == NVX_GSP_RMAPI_OK) {
        clear_ring(ring);
        return NVX_GSP_RMAPI_OK;
    }
    if (ring->memory.stage != NVX_GSP_OS_MEMORY_IDLE)
        ring->stage = NVX_GSP_CHANNEL_RING_MEMORY_DESTROY_PENDING;
    return status;
}

static nvx_gsp_rmapi_status_t finish_context_destroy(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_channel_ring_t *ring,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    if (ring->context_dma.stage == NVX_GSP_CONTEXT_DMA_CREATED) {
        status = nvx_gsp_context_dma_destroy(manager, &ring->context_dma,
            scratch, scratch_size, response, response_size, timeout_ms,
            poll_ms, delay_ms, delay_context);
        if (status != NVX_GSP_RMAPI_OK) {
            ring->stage = NVX_GSP_CHANNEL_RING_CONTEXT_DESTROY_PENDING;
            return status;
        }
    }
    return finish_memory_destroy(manager, ring, scratch, scratch_size,
        response, response_size, timeout_ms, poll_ms, delay_ms, delay_context);
}

static nvx_gsp_rmapi_status_t begin_destroy(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_channel_ring_t *ring,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    if (ring->mapping.stage == NVX_GSP_DMA_MAP_MAPPED) {
        status = nvx_gsp_os_memory_dma_unmap(manager, &ring->memory,
            &ring->context_dma, &ring->mapping, scratch, scratch_size,
            response, response_size, timeout_ms, poll_ms, delay_ms,
            delay_context);
        if (status != NVX_GSP_RMAPI_OK) {
            ring->stage = NVX_GSP_CHANNEL_RING_UNMAP_PENDING;
            return status;
        }
    }
    return finish_context_destroy(manager, ring, scratch, scratch_size,
        response, response_size, timeout_ms, poll_ms, delay_ms, delay_context);
}

nvx_gsp_rmapi_status_t nvx_gsp_channel_ring_create(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_channel_ring_t *ring,
    uint32_t client, uint32_t device, uint32_t context_dma,
    uint32_t subdevice, uint32_t entry_count,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    uint64_t bytes;
    if (!manager || !ring || ring->stage != NVX_GSP_CHANNEL_RING_IDLE ||
        !client || !device || !context_dma || client == device ||
        client == context_dma || device == context_dma || !subdevice ||
        (subdevice & (subdevice - 1U)) != 0U || !power_of_two(entry_count) ||
        !scratch || !scratch_size || !response || !response_size ||
        !timeout_ms || !poll_ms || !delay_ms)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    bytes = (uint64_t)entry_count * 8U;
    if (bytes > SIZE_MAX || bytes > UINT32_MAX)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    clear_ring(ring);
    ring->client_handle = client;
    ring->device_handle = device;
    ring->context_dma_handle = context_dma;
    ring->subdevice_handle = subdevice;
    ring->entry_count = entry_count;
    ring->bytes = (size_t)bytes;

    status = nvx_gsp_os_memory_create(manager, &ring->memory, client, device,
        context_dma, ring->bytes, NVX_GSP_CHANNEL_RING_TAG, scratch,
        scratch_size, response, response_size, timeout_ms, poll_ms,
        delay_ms, delay_context);
    if (status != NVX_GSP_RMAPI_OK) {
        if (ring->memory.stage == NVX_GSP_OS_MEMORY_ALLOC_PENDING) {
            ring->stage = NVX_GSP_CHANNEL_RING_MEMORY_CREATE_PENDING;
            return status;
        }
        if (ring->memory.stage == NVX_GSP_OS_MEMORY_CREATED) {
            ring->stage = NVX_GSP_CHANNEL_RING_MEMORY_DESTROY_PENDING;
            return status;
        }
        clear_ring(ring);
        return status;
    }

    status = nvx_gsp_os_memory_acquire(&ring->memory);
    if (status != NVX_GSP_RMAPI_OK) {
        ring->stage = NVX_GSP_CHANNEL_RING_MEMORY_DESTROY_PENDING;
        return status;
    }
    ring->memory_lease_held = 1U;
    return begin_context_create(manager, ring, scratch, scratch_size,
        response, response_size, timeout_ms, poll_ms, delay_ms, delay_context);
}

nvx_gsp_rmapi_status_t nvx_gsp_channel_ring_recover(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_channel_ring_t *ring,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    if (!manager || !ring || !scratch || !response || !timeout_ms ||
        !poll_ms || !delay_ms)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    switch ((nvx_gsp_channel_ring_stage_t)ring->stage) {
    case NVX_GSP_CHANNEL_RING_MEMORY_CREATE_PENDING:
        status = nvx_gsp_os_memory_recover(manager, &ring->memory, scratch,
            scratch_size, response, response_size, timeout_ms, poll_ms,
            delay_ms, delay_context);
        if (status != NVX_GSP_RMAPI_OK) {
            if (ring->memory.stage == NVX_GSP_OS_MEMORY_CREATED)
                ring->stage = NVX_GSP_CHANNEL_RING_MEMORY_DESTROY_PENDING;
            return status;
        }
        status = nvx_gsp_os_memory_acquire(&ring->memory);
        if (status != NVX_GSP_RMAPI_OK) {
            ring->stage = NVX_GSP_CHANNEL_RING_MEMORY_DESTROY_PENDING;
            return status;
        }
        ring->memory_lease_held = 1U;
        return begin_context_create(manager, ring, scratch, scratch_size,
            response, response_size, timeout_ms, poll_ms, delay_ms, delay_context);

    case NVX_GSP_CHANNEL_RING_CONTEXT_CREATE_PENDING:
        if (ring->context_dma.stage != NVX_GSP_CONTEXT_DMA_IDLE) {
            status = nvx_gsp_context_dma_recover(manager, &ring->context_dma,
                scratch, scratch_size, response, response_size, timeout_ms,
                poll_ms, delay_ms, delay_context);
            if (status != NVX_GSP_RMAPI_OK) {
                if (ring->context_dma.stage == NVX_GSP_CONTEXT_DMA_CREATED)
                    ring->stage = NVX_GSP_CHANNEL_RING_CONTEXT_DESTROY_PENDING;
                return status;
            }
        }
        if (ring->context_dma.stage != NVX_GSP_CONTEXT_DMA_CREATED) {
            ring->stage = NVX_GSP_CHANNEL_RING_RING_MEMORY_DESTROY_PENDING;
            return NVX_GSP_RMAPI_REMOTE_ERROR;
        }
        return begin_context_create(manager, ring, scratch, scratch_size,
            response, response_size, timeout_ms, poll_ms, delay_ms, delay_context);

    case NVX_GSP_CHANNEL_RING_MAP_PENDING:
        status = nvx_gsp_os_memory_dma_recover(manager, &ring->memory,
            &ring->context_dma, &ring->mapping, scratch, scratch_size,
            response, response_size, timeout_ms, poll_ms, delay_ms,
            delay_context);
        if (status != NVX_GSP_RMAPI_OK) {
            if (ring->mapping.stage == NVX_GSP_DMA_MAP_MAPPED)
                ring->stage = NVX_GSP_CHANNEL_RING_UNMAP_PENDING;
            return status;
        }
        ring->gpu_virtual_address = ring->mapping.dma_offset;
        if ((ring->gpu_virtual_address & 7U) != 0U ||
            ring->gpu_virtual_address >= NVX_GSP_USERD_GPUVA_LIMIT ||
            (uint64_t)ring->bytes >
                NVX_GSP_USERD_GPUVA_LIMIT - ring->gpu_virtual_address) {
            ring->stage = NVX_GSP_CHANNEL_RING_UNMAP_PENDING;
            return NVX_GSP_RMAPI_MALFORMED_RESPONSE;
        }
        ring->entries = (volatile uint32_t *)ring->memory.backing.cpu_address;
        if (!ring->entries || ((uintptr_t)ring->entries & 7U) != 0U) {
            ring->stage = NVX_GSP_CHANNEL_RING_UNMAP_PENDING;
            return NVX_GSP_RMAPI_INVALID_ARGUMENT;
        }
        memset((void *)ring->entries, 0, ring->bytes);
        ring->stage = NVX_GSP_CHANNEL_RING_READY;
        return NVX_GSP_RMAPI_OK;

    case NVX_GSP_CHANNEL_RING_UNMAP_PENDING:
        if (ring->mapping.stage == NVX_GSP_DMA_MAP_UNMAPPING ||
            ring->mapping.stage == NVX_GSP_DMA_MAP_UNMAP_RELEASE_PENDING) {
            status = nvx_gsp_os_memory_dma_recover(manager, &ring->memory,
                &ring->context_dma, &ring->mapping, scratch, scratch_size,
                response, response_size, timeout_ms, poll_ms, delay_ms,
                delay_context);
            if (status != NVX_GSP_RMAPI_OK)
                return status;
        }
        if (ring->mapping.stage == NVX_GSP_DMA_MAP_MAPPED)
            return begin_destroy(manager, ring, scratch, scratch_size,
                response, response_size, timeout_ms, poll_ms, delay_ms,
                delay_context);
        return finish_context_destroy(manager, ring, scratch, scratch_size,
            response, response_size, timeout_ms, poll_ms, delay_ms, delay_context);

    case NVX_GSP_CHANNEL_RING_CONTEXT_DESTROY_PENDING:
        if (ring->context_dma.stage == NVX_GSP_CONTEXT_DMA_FREE_PENDING ||
            ring->context_dma.stage == NVX_GSP_CONTEXT_DMA_FREE_RELEASE_PENDING) {
            status = nvx_gsp_context_dma_recover(manager, &ring->context_dma,
                scratch, scratch_size, response, response_size, timeout_ms,
                poll_ms, delay_ms, delay_context);
            if (status != NVX_GSP_RMAPI_OK)
                return status;
        }
        if (ring->context_dma.stage == NVX_GSP_CONTEXT_DMA_CREATED)
            return finish_context_destroy(manager, ring, scratch, scratch_size,
                response, response_size, timeout_ms, poll_ms, delay_ms, delay_context);
        return finish_memory_destroy(manager, ring, scratch, scratch_size,
            response, response_size, timeout_ms, poll_ms, delay_ms, delay_context);

    case NVX_GSP_CHANNEL_RING_MEMORY_DESTROY_PENDING:
        if (ring->memory.stage == NVX_GSP_OS_MEMORY_FREE_PENDING ||
            ring->memory.stage == NVX_GSP_OS_MEMORY_FREE_RELEASE_PENDING) {
            status = nvx_gsp_os_memory_recover(manager, &ring->memory,
                scratch, scratch_size, response, response_size, timeout_ms,
                poll_ms, delay_ms, delay_context);
            if (status != NVX_GSP_RMAPI_OK)
                return status;
        }
        if (ring->memory.stage == NVX_GSP_OS_MEMORY_IDLE) {
            clear_ring(ring);
            return NVX_GSP_RMAPI_OK;
        }
        return finish_memory_destroy(manager, ring, scratch, scratch_size,
            response, response_size, timeout_ms, poll_ms, delay_ms, delay_context);

    case NVX_GSP_CHANNEL_RING_READY:
        return NVX_GSP_RMAPI_OK;
    default:
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    }
}

nvx_gsp_rmapi_status_t nvx_gsp_channel_ring_acquire(
    nvx_gsp_channel_ring_t *ring)
{
    uint32_t refs;
    if (!ring || ring->stage != NVX_GSP_CHANNEL_RING_READY)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    refs = __atomic_load_n(&ring->references, __ATOMIC_ACQUIRE);
    for (;;) {
        if (refs == UINT32_MAX || ring->stage != NVX_GSP_CHANNEL_RING_READY)
            return NVX_GSP_RMAPI_BUSY;
        if (__atomic_compare_exchange_n(&ring->references, &refs, refs + 1U,
                0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return NVX_GSP_RMAPI_OK;
    }
}

nvx_gsp_rmapi_status_t nvx_gsp_channel_ring_release(
    nvx_gsp_channel_ring_t *ring)
{
    uint32_t refs;
    if (!ring)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    refs = __atomic_load_n(&ring->references, __ATOMIC_ACQUIRE);
    for (;;) {
        if (!refs)
            return NVX_GSP_RMAPI_INVALID_ARGUMENT;
        if (__atomic_compare_exchange_n(&ring->references, &refs, refs - 1U,
                0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return NVX_GSP_RMAPI_OK;
    }
}

nvx_gsp_rmapi_status_t nvx_gsp_channel_ring_destroy(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_channel_ring_t *ring,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    if (!manager || !ring || !scratch || !response || !timeout_ms ||
        !poll_ms || !delay_ms || ring->references != 0U)
        return ring && ring->references ? NVX_GSP_RMAPI_BUSY :
            NVX_GSP_RMAPI_INVALID_ARGUMENT;

    if (ring->stage == NVX_GSP_CHANNEL_RING_READY) {
        ring->stage = NVX_GSP_CHANNEL_RING_UNMAP_PENDING;
        return begin_destroy(manager, ring, scratch, scratch_size,
            response, response_size, timeout_ms, poll_ms, delay_ms,
            delay_context);
    }
    if (ring->stage == NVX_GSP_CHANNEL_RING_MEMORY_DESTROY_PENDING)
        return finish_memory_destroy(manager, ring, scratch, scratch_size,
            response, response_size, timeout_ms, poll_ms, delay_ms, delay_context);
    if (ring->stage == NVX_GSP_CHANNEL_RING_CONTEXT_DESTROY_PENDING)
        return finish_context_destroy(manager, ring, scratch, scratch_size,
            response, response_size, timeout_ms, poll_ms, delay_ms, delay_context);
    if (ring->stage == NVX_GSP_CHANNEL_RING_UNMAP_PENDING) {
        if (ring->mapping.stage == NVX_GSP_DMA_MAP_MAPPED)
            return begin_destroy(manager, ring, scratch, scratch_size,
                response, response_size, timeout_ms, poll_ms, delay_ms,
                delay_context);
        return nvx_gsp_channel_ring_recover(manager, ring, scratch,
            scratch_size, response, response_size, timeout_ms, poll_ms,
            delay_ms, delay_context);
    }
    if (ring->stage == NVX_GSP_CHANNEL_RING_MEMORY_CREATE_PENDING ||
        ring->stage == NVX_GSP_CHANNEL_RING_CONTEXT_CREATE_PENDING ||
        ring->stage == NVX_GSP_CHANNEL_RING_MAP_PENDING)
        return nvx_gsp_channel_ring_recover(manager, ring, scratch,
            scratch_size, response, response_size, timeout_ms, poll_ms,
            delay_ms, delay_context);
    return NVX_GSP_RMAPI_INVALID_ARGUMENT;
}
