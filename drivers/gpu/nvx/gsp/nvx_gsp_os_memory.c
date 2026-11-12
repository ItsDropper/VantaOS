/*
 * Copyright (c) 2026 ItsDropper
 * Licensed under the MIT License; see LICENSE.
 */
#include "nvx_gsp_os_memory.h"
#include "gpu_dma.h"
#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(nvx_gsp_os_memory_params_t) ==
    NVX_GSP_OS_MEMORY_DESCRIPTOR_PARAMS_SIZE, "NV_OS_DESC_MEMORY ABI size");
_Static_assert(offsetof(nvx_gsp_os_memory_params_t, descriptor) == 16U,
    "NV_OS_DESC_MEMORY descriptor offset");
_Static_assert(offsetof(nvx_gsp_os_memory_params_t, limit) == 24U,
    "NV_OS_DESC_MEMORY limit offset");
_Static_assert(offsetof(nvx_gsp_os_memory_params_t, descriptor_type) == 32U,
    "NV_OS_DESC_MEMORY descriptorType offset");


#define NVX_GSP_OS_MEMORY_LEASE_CLOSED UINT32_C(0x80000000)
#define NVX_GSP_OS_MEMORY_LEASE_COUNT_MASK UINT32_C(0x7FFFFFFF)

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_acquire(nvx_gsp_os_memory_t *m)
{
    uint32_t state;
    if (!m || m->stage != NVX_GSP_OS_MEMORY_CREATED || !m->remote_created ||
        !m->backing.acquired)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    state = __atomic_load_n(&m->lifetime_state, __ATOMIC_ACQUIRE);
    for (;;) {
        uint32_t desired;
        if ((state & NVX_GSP_OS_MEMORY_LEASE_CLOSED) != 0U ||
            (state & NVX_GSP_OS_MEMORY_LEASE_COUNT_MASK) ==
                NVX_GSP_OS_MEMORY_LEASE_COUNT_MASK)
            return NVX_GSP_RMAPI_BUSY;
        desired = state + 1U;
        if (__atomic_compare_exchange_n(&m->lifetime_state, &state, desired,
                0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return NVX_GSP_RMAPI_OK;
    }
}

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_release(nvx_gsp_os_memory_t *m)
{
    uint32_t state;
    if (!m)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    state = __atomic_load_n(&m->lifetime_state, __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & NVX_GSP_OS_MEMORY_LEASE_CLOSED) != 0U ||
            (state & NVX_GSP_OS_MEMORY_LEASE_COUNT_MASK) == 0U)
            return NVX_GSP_RMAPI_INVALID_ARGUMENT;
        if (__atomic_compare_exchange_n(&m->lifetime_state, &state,
                state - 1U, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return NVX_GSP_RMAPI_OK;
    }
}

static int overlaps(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t ap, bp;
    if (!a || !b || !an || !bn) return 0;
    ap = (uintptr_t)a; bp = (uintptr_t)b;
    if (an > UINTPTR_MAX - ap || bn > UINTPTR_MAX - bp) return 1;
    return ap < bp + bn && bp < ap + an;
}

static int idle_valid(const nvx_gsp_os_memory_t *m)
{
    return m && m->stage == NVX_GSP_OS_MEMORY_IDLE &&
        !m->client_handle && !m->device_handle && !m->memory_handle &&
        !m->requested_size && !m->backing.acquired && !m->backing.pages &&
        !m->operation.handle.valid && !m->remote_created &&
        m->lifetime_state == 0U;
}

static void clear_state(nvx_gsp_os_memory_t *m)
{
    m->client_handle = m->device_handle = m->memory_handle = 0U;
    m->requested_size = 0U;
    m->operation = (nvx_gsp_rmapi_result_t){0};
    m->completed_status = NVX_GSP_RMAPI_OK;
    m->lifetime_state = 0U;
    m->remote_created = 0U;
    m->stage = NVX_GSP_OS_MEMORY_IDLE;
}

static void release_backing(nvx_gsp_os_memory_t *m)
{
    if (m->backing.acquired) nvx_dma_release(&m->backing);
}

static int alloc_operation(uint8_t stage)
{
    return stage == NVX_GSP_OS_MEMORY_ALLOC_PENDING ||
        stage == NVX_GSP_OS_MEMORY_ALLOC_RELEASE_PENDING ||
        stage == NVX_GSP_OS_MEMORY_ALLOC_QUARANTINED;
}

static nvx_gsp_rmapi_status_t finish_release(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_os_memory_t *m)
{
    nvx_gsp_rmapi_status_t completed = m->completed_status;
    if (nvx_gsp_rpc_release(manager, &m->operation.handle) != NVX_GSP_RPC_OK)
        return NVX_GSP_RMAPI_RPC_ERROR;

    if (m->stage == NVX_GSP_OS_MEMORY_ALLOC_RELEASE_PENDING) {
        if (completed == NVX_GSP_RMAPI_OK) {
            m->remote_created = 1U;
            m->stage = NVX_GSP_OS_MEMORY_CREATED;
            return NVX_GSP_RMAPI_OK;
        }
        release_backing(m);
        clear_state(m);
        return completed;
    }
    if (m->stage == NVX_GSP_OS_MEMORY_FREE_RELEASE_PENDING) {
        if (completed == NVX_GSP_RMAPI_OK) {
            release_backing(m);
            clear_state(m);
            return NVX_GSP_RMAPI_OK;
        }
        m->stage = NVX_GSP_OS_MEMORY_CREATED;
        return completed;
    }
    return NVX_GSP_RMAPI_INVALID_ARGUMENT;
}

static nvx_gsp_rmapi_status_t wait_operation(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_os_memory_t *m,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    if (m->stage == NVX_GSP_OS_MEMORY_ALLOC_RELEASE_PENDING ||
        m->stage == NVX_GSP_OS_MEMORY_FREE_RELEASE_PENDING)
        return finish_release(manager, m);

    status = nvx_gsp_rmapi_wait(manager, &m->operation, scratch, scratch_size,
        response, response_capacity, timeout_ms, poll_interval_ms,
        delay_ms, delay_context);
    if (status == NVX_GSP_RMAPI_TIMEOUT || status == NVX_GSP_RMAPI_RPC_ERROR)
        return status;

    /*
     * A malformed/truncated completed response does not prove whether RM
     * created/freed the object. Keep the physical pages pinned permanently
     * until the caller resets the GPU and explicitly handles this quarantine.
     */
    if (status == NVX_GSP_RMAPI_MALFORMED_RESPONSE ||
        status == NVX_GSP_RMAPI_RESPONSE_TOO_SMALL) {
        m->stage = alloc_operation(m->stage) ?
            NVX_GSP_OS_MEMORY_ALLOC_QUARANTINED :
            NVX_GSP_OS_MEMORY_FREE_QUARANTINED;
        return status;
    }

    m->completed_status = status;
    m->stage = alloc_operation(m->stage) ?
        NVX_GSP_OS_MEMORY_ALLOC_RELEASE_PENDING :
        NVX_GSP_OS_MEMORY_FREE_RELEASE_PENDING;
    return finish_release(manager, m);
}

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_create(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_os_memory_t *m,
    uint32_t client, uint32_t device, uint32_t memory,
    size_t size, uint32_t tag,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_os_memory_params_t p = {0};
    nvx_gsp_rmapi_status_t status;
    nvx_dma_status_t dma_status;

    if (!manager || !m || !client || !device || !memory || !size ||
        size > NVX_DMA_MAX_BYTES || !scratch || !scratch_size || !response ||
        response_capacity < 32U || !timeout_ms || !poll_interval_ms ||
        !delay_ms || overlaps(m, sizeof(*m), manager, sizeof(*manager)) ||
        overlaps(m, sizeof(*m), scratch, scratch_size) ||
        overlaps(m, sizeof(*m), response, response_capacity) ||
        overlaps(scratch, scratch_size, response, response_capacity) ||
        !idle_valid(m))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    dma_status = nvx_dma_alloc_identity_mapped(size, &m->backing);
    if (dma_status != NVX_DMA_OK) return NVX_GSP_RMAPI_NO_SPACE;

    if (!gpu_dma_buffer_is_valid(&m->backing) ||
        m->backing.allocated_size < size || !m->backing.allocated_size ||
        (m->backing.gpu_address & (NVX_DMA_PAGE_SIZE - 1U)) ||
        m->backing.allocated_size - 1U >
            UINT64_MAX - m->backing.gpu_address ||
        m->backing.cpu_address != (void *)(uintptr_t)m->backing.gpu_address) {
        release_backing(m);
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    }

    p.type = NVX_GSP_OS_MEMORY_TYPE_IMAGE;
    p.attr = NVX_GSP_OS_MEMORY_ATTR_SYSTEM_CONTIGUOUS_CACHED;
    p.attr2 = NVX_GSP_OS_MEMORY_ATTR2_GPU_CACHEABLE_NO;
    p.descriptor = m->backing.gpu_address;
    p.limit = m->backing.allocated_size - 1U;
    p.descriptor_type = NVX_GSP_OS_MEMORY_DESCRIPTOR_OS_PHYS_ADDR;
    p.tag = tag;

    m->client_handle = client;
    m->device_handle = device;
    m->memory_handle = memory;
    m->requested_size = size;
    m->completed_status = NVX_GSP_RMAPI_OK;
    status = nvx_gsp_rm_alloc_submit(manager, client, device, memory,
        NVX_GSP_RM_CLASS_MEMORY_SYSTEM_OS_DESCRIPTOR, 0U, &p, sizeof(p),
        scratch, scratch_size, response, response_capacity, &m->operation);
    if (status != NVX_GSP_RMAPI_OK && !m->operation.handle.valid) {
        release_backing(m);
        clear_state(m);
        return status;
    }
    m->stage = NVX_GSP_OS_MEMORY_ALLOC_PENDING;
    return wait_operation(manager, m, scratch, scratch_size, response,
        response_capacity, timeout_ms, poll_interval_ms, delay_ms,
        delay_context);
}

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_recover(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_os_memory_t *m,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    if (!manager || !m || !scratch || !scratch_size || !response ||
        !response_capacity || !timeout_ms || !poll_interval_ms || !delay_ms ||
        overlaps(m, sizeof(*m), manager, sizeof(*manager)) ||
        overlaps(m, sizeof(*m), scratch, scratch_size) ||
        overlaps(m, sizeof(*m), response, response_capacity) ||
        overlaps(scratch, scratch_size, response, response_capacity) ||
        !m->operation.handle.valid ||
        (m->stage != NVX_GSP_OS_MEMORY_ALLOC_PENDING &&
         m->stage != NVX_GSP_OS_MEMORY_ALLOC_RELEASE_PENDING &&
         m->stage != NVX_GSP_OS_MEMORY_FREE_PENDING &&
         m->stage != NVX_GSP_OS_MEMORY_FREE_RELEASE_PENDING &&
         m->stage != NVX_GSP_OS_MEMORY_ALLOC_QUARANTINED &&
         m->stage != NVX_GSP_OS_MEMORY_FREE_QUARANTINED))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    if (m->stage == NVX_GSP_OS_MEMORY_ALLOC_QUARANTINED ||
        m->stage == NVX_GSP_OS_MEMORY_FREE_QUARANTINED)
        m->stage = m->stage == NVX_GSP_OS_MEMORY_ALLOC_QUARANTINED ?
            NVX_GSP_OS_MEMORY_ALLOC_PENDING : NVX_GSP_OS_MEMORY_FREE_PENDING;
    {
        nvx_gsp_rmapi_status_t status = wait_operation(manager, m, scratch,
            scratch_size, response, response_capacity, timeout_ms,
            poll_interval_ms, delay_ms, delay_context);
        if (m->stage == NVX_GSP_OS_MEMORY_CREATED)
            __atomic_store_n(&m->lifetime_state, 0U, __ATOMIC_RELEASE);
        return status;
    }
}

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_destroy(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_os_memory_t *m,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    nvx_gsp_rmapi_status_t status;
    if (!manager || !m || !scratch || !scratch_size || !response ||
        !response_capacity || !timeout_ms || !poll_interval_ms || !delay_ms ||
        m->stage != NVX_GSP_OS_MEMORY_CREATED || !m->remote_created ||
        !m->backing.acquired ||
        overlaps(m, sizeof(*m), manager, sizeof(*manager)) ||
        overlaps(m, sizeof(*m), scratch, scratch_size) ||
        overlaps(m, sizeof(*m), response, response_capacity) ||
        overlaps(scratch, scratch_size, response, response_capacity))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;

    {
        uint32_t expected = 0U;
        if (!__atomic_compare_exchange_n(&m->lifetime_state, &expected,
                NVX_GSP_OS_MEMORY_LEASE_CLOSED, 0,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return NVX_GSP_RMAPI_BUSY;
    }

    status = nvx_gsp_rm_free_submit(manager, m->client_handle,
        m->device_handle, m->memory_handle, scratch, scratch_size, response,
        response_capacity, &m->operation);
    if (status != NVX_GSP_RMAPI_OK && !m->operation.handle.valid) {
        __atomic_store_n(&m->lifetime_state, 0U, __ATOMIC_RELEASE);
        return status;
    }
    m->stage = NVX_GSP_OS_MEMORY_FREE_PENDING;
    status = wait_operation(manager, m, scratch, scratch_size, response,
        response_capacity, timeout_ms, poll_interval_ms, delay_ms,
        delay_context);
    if (m->stage == NVX_GSP_OS_MEMORY_CREATED)
        __atomic_store_n(&m->lifetime_state, 0U, __ATOMIC_RELEASE);
    return status;
}

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_get_cpu_address(
    const nvx_gsp_os_memory_t *m, void **out)
{
    if (out) *out = NULL;
    if (!m || !out || m->stage != NVX_GSP_OS_MEMORY_CREATED ||
        !m->remote_created || !gpu_dma_buffer_is_valid(&m->backing))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    *out = m->backing.cpu_address;
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_get_physical_range(
    const nvx_gsp_os_memory_t *m, uint64_t *base, uint64_t *size)
{
    if (base) *base = 0U;
    if (size) *size = 0U;
    if (!m || !base || !size || m->stage != NVX_GSP_OS_MEMORY_CREATED ||
        !m->remote_created || !gpu_dma_buffer_is_valid(&m->backing))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    *base = m->backing.gpu_address;
    *size = m->backing.allocated_size;
    return NVX_GSP_RMAPI_OK;
}
