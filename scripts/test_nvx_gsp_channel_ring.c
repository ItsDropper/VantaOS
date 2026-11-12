#include "nvx_gsp_channel_ring.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

_Alignas(4096) static uint8_t backing[4096];
static unsigned memory_creates, context_creates, maps, unmaps;
static unsigned context_destroys, memory_destroys;
static unsigned timeout_memory_create, timeout_context_create, timeout_map;
static unsigned timeout_unmap, timeout_context_destroy, timeout_memory_destroy;

static void delay(uint32_t ms, void *context)
{
    (void)ms;
    (void)context;
}

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_create(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_os_memory_t *memory,
    uint32_t client, uint32_t device, uint32_t handle, size_t size,
    uint32_t tag, void *scratch, size_t scratch_size, void *response,
    size_t response_size, uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    (void)manager; (void)scratch; (void)scratch_size; (void)response;
    (void)response_size; (void)timeout_ms; (void)poll_ms;
    (void)delay_ms; (void)delay_context;
    ++memory_creates;
    assert(client == 0x10U && device == 0x20U && handle == 0x41U);
    assert(size == sizeof(backing) && tag == NVX_GSP_CHANNEL_RING_TAG);
    memory->client_handle = client;
    memory->device_handle = device;
    memory->memory_handle = handle;
    memory->requested_size = size;
    memory->backing.acquired = 1U;
    memory->backing.cpu_address = backing;
    if (timeout_memory_create) {
        timeout_memory_create = 0U;
        memory->stage = NVX_GSP_OS_MEMORY_ALLOC_PENDING;
        memory->operation.handle.valid = 1U;
        memory->operation.function = NVX_GSP_RPC_FN_RM_ALLOC;
        return NVX_GSP_RMAPI_TIMEOUT;
    }
    memory->stage = NVX_GSP_OS_MEMORY_CREATED;
    memory->remote_created = 1U;
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_acquire(nvx_gsp_os_memory_t *memory)
{
    assert(memory && memory->stage == NVX_GSP_OS_MEMORY_CREATED);
    ++memory->lifetime_state;
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_release(nvx_gsp_os_memory_t *memory)
{
    assert(memory && memory->lifetime_state != 0U);
    --memory->lifetime_state;
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_recover(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_os_memory_t *memory,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    (void)manager; (void)scratch; (void)scratch_size; (void)response;
    (void)response_size; (void)timeout_ms; (void)poll_ms;
    (void)delay_ms; (void)delay_context;
    memory->operation.handle.valid = 0U;
    if (memory->stage == NVX_GSP_OS_MEMORY_ALLOC_PENDING) {
        memory->stage = NVX_GSP_OS_MEMORY_CREATED;
        memory->remote_created = 1U;
    } else if (memory->stage == NVX_GSP_OS_MEMORY_FREE_PENDING ||
               memory->stage == NVX_GSP_OS_MEMORY_FREE_RELEASE_PENDING) {
        memory->stage = NVX_GSP_OS_MEMORY_IDLE;
        memory->remote_created = 0U;
        memory->backing.acquired = 0U;
    }
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_destroy(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_os_memory_t *memory,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    (void)manager; (void)scratch; (void)scratch_size; (void)response;
    (void)response_size; (void)timeout_ms; (void)poll_ms;
    (void)delay_ms; (void)delay_context;
    assert(memory->lifetime_state == 0U);
    ++memory_destroys;
    if (timeout_memory_destroy) {
        timeout_memory_destroy = 0U;
        memory->stage = NVX_GSP_OS_MEMORY_FREE_PENDING;
        memory->operation.handle.valid = 1U;
        memory->operation.function = NVX_GSP_RPC_FN_FREE;
        return NVX_GSP_RMAPI_TIMEOUT;
    }
    memory->stage = NVX_GSP_OS_MEMORY_IDLE;
    memory->remote_created = 0U;
    memory->backing.acquired = 0U;
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_context_dma_create(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_context_dma_t *context_dma,
    uint32_t client, uint32_t device, uint32_t handle, uint32_t memory,
    uint32_t subdevice, uint32_t flags, uint64_t offset, uint64_t limit,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    (void)manager; (void)scratch; (void)scratch_size; (void)response;
    (void)response_size; (void)timeout_ms; (void)poll_ms;
    (void)delay_ms; (void)delay_context;
    ++context_creates;
    assert(client == 0x10U && device == 0x20U && handle == 0x30U);
    assert(memory == 0x41U && subdevice == 1U && flags == 0U && offset == 0U);
    assert(limit == sizeof(backing) - 1U);
    context_dma->client_handle = client;
    context_dma->device_handle = device;
    context_dma->context_dma_handle = handle;
    context_dma->params.h_memory = memory;
    context_dma->params.offset = offset;
    context_dma->params.limit = limit;
    if (timeout_context_create) {
        timeout_context_create = 0U;
        context_dma->stage = NVX_GSP_CONTEXT_DMA_ALLOC_PENDING;
        context_dma->operation.handle.valid = 1U;
        context_dma->operation.function = NVX_GSP_RPC_FN_RM_ALLOC;
        return NVX_GSP_RMAPI_TIMEOUT;
    }
    context_dma->stage = NVX_GSP_CONTEXT_DMA_CREATED;
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_context_dma_recover(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_context_dma_t *context_dma,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    (void)manager; (void)scratch; (void)scratch_size; (void)response;
    (void)response_size; (void)timeout_ms; (void)poll_ms;
    (void)delay_ms; (void)delay_context;
    context_dma->operation.handle.valid = 0U;
    if (context_dma->stage == NVX_GSP_CONTEXT_DMA_ALLOC_PENDING)
        context_dma->stage = NVX_GSP_CONTEXT_DMA_CREATED;
    else if (context_dma->stage == NVX_GSP_CONTEXT_DMA_FREE_PENDING ||
             context_dma->stage == NVX_GSP_CONTEXT_DMA_FREE_RELEASE_PENDING)
        context_dma->stage = NVX_GSP_CONTEXT_DMA_IDLE;
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_context_dma_destroy(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_context_dma_t *context_dma,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    (void)manager; (void)scratch; (void)scratch_size; (void)response;
    (void)response_size; (void)timeout_ms; (void)poll_ms;
    (void)delay_ms; (void)delay_context;
    ++context_destroys;
    if (timeout_context_destroy) {
        timeout_context_destroy = 0U;
        context_dma->stage = NVX_GSP_CONTEXT_DMA_FREE_PENDING;
        context_dma->operation.handle.valid = 1U;
        context_dma->operation.function = NVX_GSP_RPC_FN_FREE;
        return NVX_GSP_RMAPI_TIMEOUT;
    }
    context_dma->stage = NVX_GSP_CONTEXT_DMA_IDLE;
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_dma_map(
    nvx_gsp_rpc_manager_t *manager, const nvx_gsp_os_memory_t *memory,
    const nvx_gsp_context_dma_t *context_dma, nvx_gsp_dma_mapping_t *mapping,
    uint64_t offset, uint64_t length, uint32_t flags, uint32_t flags2,
    uint32_t kind_override, uint64_t dma_offset,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    (void)manager; (void)memory; (void)context_dma; (void)flags; (void)flags2;
    (void)kind_override; (void)dma_offset; (void)scratch; (void)scratch_size;
    (void)response; (void)response_size; (void)timeout_ms; (void)poll_ms;
    (void)delay_ms; (void)delay_context;
    ++maps;
    assert(offset == 0U && length == sizeof(backing));
    mapping->request.client = 0x10U;
    mapping->request.device = 0x20U;
    mapping->request.dma = 0x30U;
    mapping->request.memory = 0x41U;
    mapping->request.offset = offset;
    mapping->request.length = length;
    if (timeout_map) {
        timeout_map = 0U;
        mapping->stage = NVX_GSP_DMA_MAP_MAPPING;
        mapping->operation.handle.valid = 1U;
        mapping->operation.function = NVX_GSP_RPC_FN_MAP_MEMORY_DMA;
        return NVX_GSP_RMAPI_TIMEOUT;
    }
    mapping->dma_offset = 0x200000U;
    mapping->stage = NVX_GSP_DMA_MAP_MAPPED;
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_dma_recover(
    nvx_gsp_rpc_manager_t *manager, const nvx_gsp_os_memory_t *memory,
    const nvx_gsp_context_dma_t *context_dma, nvx_gsp_dma_mapping_t *mapping,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    (void)manager; (void)memory; (void)context_dma; (void)scratch;
    (void)scratch_size; (void)response; (void)response_size;
    (void)timeout_ms; (void)poll_ms; (void)delay_ms; (void)delay_context;
    mapping->operation.handle.valid = 0U;
    if (mapping->stage == NVX_GSP_DMA_MAP_MAPPING) {
        mapping->dma_offset = 0x200000U;
        mapping->stage = NVX_GSP_DMA_MAP_MAPPED;
    } else if (mapping->stage == NVX_GSP_DMA_MAP_UNMAPPING ||
               mapping->stage == NVX_GSP_DMA_MAP_UNMAP_RELEASE_PENDING) {
        mapping->stage = NVX_GSP_DMA_MAP_IDLE;
        mapping->dma_offset = 0U;
    }
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_os_memory_dma_unmap(
    nvx_gsp_rpc_manager_t *manager, const nvx_gsp_os_memory_t *memory,
    const nvx_gsp_context_dma_t *context_dma, nvx_gsp_dma_mapping_t *mapping,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    (void)manager; (void)memory; (void)context_dma; (void)scratch;
    (void)scratch_size; (void)response; (void)response_size;
    (void)timeout_ms; (void)poll_ms; (void)delay_ms; (void)delay_context;
    ++unmaps;
    if (timeout_unmap) {
        timeout_unmap = 0U;
        mapping->stage = NVX_GSP_DMA_MAP_UNMAPPING;
        mapping->operation.handle.valid = 1U;
        mapping->operation.function = NVX_GSP_RPC_FN_UNMAP_MEMORY_DMA;
        return NVX_GSP_RMAPI_TIMEOUT;
    }
    mapping->stage = NVX_GSP_DMA_MAP_IDLE;
    mapping->dma_offset = 0U;
    return NVX_GSP_RMAPI_OK;
}

static void reset_counters(void)
{
    memory_creates = context_creates = maps = unmaps = 0U;
    context_destroys = memory_destroys = 0U;
}

static void create_ring(nvx_gsp_rpc_manager_t *manager,
    nvx_gsp_channel_ring_t *ring, uint8_t *scratch, uint8_t *response)
{
    assert(nvx_gsp_channel_ring_create(manager, ring, 0x10U, 0x20U,
        0x41U, 0x30U, 1U, 16U, scratch, 512U, response, 512U,
        20U, 1U, delay, NULL) == NVX_GSP_RMAPI_OK);
    assert(ring->stage == NVX_GSP_CHANNEL_RING_READY);
    assert(ring->bytes == 128U && ring->allocation_bytes == 4096U);
    assert(ring->entries == (volatile uint32_t *)backing);
    assert(ring->gpu_virtual_address == 0x200000U);
}

int main(void)
{
    nvx_gsp_rpc_manager_t manager = {0};
    nvx_gsp_channel_ring_t ring = {0};
    uint8_t scratch[512], response[512];

    assert(nvx_gsp_channel_ring_destroy(&manager, &ring, scratch,
        sizeof(scratch), response, sizeof(response), 20U, 1U, delay, NULL) ==
        NVX_GSP_RMAPI_OK);
    create_ring(&manager, &ring, scratch, response);
    assert(nvx_gsp_channel_ring_acquire(&ring) == NVX_GSP_RMAPI_OK);
    assert(nvx_gsp_channel_ring_destroy(&manager, &ring, scratch,
        sizeof(scratch), response, sizeof(response), 20U, 1U, delay, NULL) ==
        NVX_GSP_RMAPI_BUSY);
    assert(nvx_gsp_channel_ring_release(&ring) == NVX_GSP_RMAPI_OK);
    assert(nvx_gsp_channel_ring_destroy(&manager, &ring, scratch,
        sizeof(scratch), response, sizeof(response), 20U, 1U, delay, NULL) ==
        NVX_GSP_RMAPI_OK);
    assert(ring.stage == NVX_GSP_CHANNEL_RING_IDLE);
    assert(unmaps == 1U && context_destroys == 1U && memory_destroys == 1U);

    memset(&ring, 0, sizeof(ring));
    reset_counters();
    timeout_memory_create = 1U;
    assert(nvx_gsp_channel_ring_create(&manager, &ring, 0x10U, 0x20U,
        0x41U, 0x30U, 1U, 16U, scratch, sizeof(scratch), response,
        sizeof(response), 20U, 1U, delay, NULL) == NVX_GSP_RMAPI_TIMEOUT);
    assert(ring.stage == NVX_GSP_CHANNEL_RING_MEMORY_CREATE_PENDING);
    assert(nvx_gsp_channel_ring_recover(&manager, &ring, scratch,
        sizeof(scratch), response, sizeof(response), 20U, 1U, delay, NULL) ==
        NVX_GSP_RMAPI_OK);
    assert(ring.stage == NVX_GSP_CHANNEL_RING_READY);
    assert(nvx_gsp_channel_ring_destroy(&manager, &ring, scratch,
        sizeof(scratch), response, sizeof(response), 20U, 1U, delay, NULL) ==
        NVX_GSP_RMAPI_OK);

    memset(&ring, 0, sizeof(ring));
    reset_counters();
    timeout_context_create = 1U;
    assert(nvx_gsp_channel_ring_create(&manager, &ring, 0x10U, 0x20U,
        0x41U, 0x30U, 1U, 16U, scratch, sizeof(scratch), response,
        sizeof(response), 20U, 1U, delay, NULL) == NVX_GSP_RMAPI_TIMEOUT);
    assert(ring.stage == NVX_GSP_CHANNEL_RING_CONTEXT_CREATE_PENDING);
    assert(nvx_gsp_channel_ring_recover(&manager, &ring, scratch,
        sizeof(scratch), response, sizeof(response), 20U, 1U, delay, NULL) ==
        NVX_GSP_RMAPI_OK);
    assert(ring.stage == NVX_GSP_CHANNEL_RING_READY);
    timeout_unmap = 1U;
    assert(nvx_gsp_channel_ring_destroy(&manager, &ring, scratch,
        sizeof(scratch), response, sizeof(response), 20U, 1U, delay, NULL) ==
        NVX_GSP_RMAPI_TIMEOUT);
    assert(ring.stage == NVX_GSP_CHANNEL_RING_UNMAP_PENDING);
    assert(nvx_gsp_channel_ring_recover(&manager, &ring, scratch,
        sizeof(scratch), response, sizeof(response), 20U, 1U, delay, NULL) ==
        NVX_GSP_RMAPI_OK);
    assert(ring.stage == NVX_GSP_CHANNEL_RING_IDLE);

    memset(&ring, 0, sizeof(ring));
    reset_counters();
    timeout_map = 1U;
    assert(nvx_gsp_channel_ring_create(&manager, &ring, 0x10U, 0x20U,
        0x41U, 0x30U, 1U, 16U, scratch, sizeof(scratch), response,
        sizeof(response), 20U, 1U, delay, NULL) == NVX_GSP_RMAPI_TIMEOUT);
    assert(ring.stage == NVX_GSP_CHANNEL_RING_MAP_PENDING);
    assert(nvx_gsp_channel_ring_recover(&manager, &ring, scratch,
        sizeof(scratch), response, sizeof(response), 20U, 1U, delay, NULL) ==
        NVX_GSP_RMAPI_OK);
    assert(ring.stage == NVX_GSP_CHANNEL_RING_READY);
    timeout_context_destroy = 1U;
    assert(nvx_gsp_channel_ring_destroy(&manager, &ring, scratch,
        sizeof(scratch), response, sizeof(response), 20U, 1U, delay, NULL) ==
        NVX_GSP_RMAPI_TIMEOUT);
    assert(ring.stage == NVX_GSP_CHANNEL_RING_CONTEXT_DESTROY_PENDING);
    assert(nvx_gsp_channel_ring_recover(&manager, &ring, scratch,
        sizeof(scratch), response, sizeof(response), 20U, 1U, delay, NULL) ==
        NVX_GSP_RMAPI_OK);
    assert(ring.stage == NVX_GSP_CHANNEL_RING_IDLE);

    memset(&ring, 0, sizeof(ring));
    reset_counters();
    timeout_memory_destroy = 1U;
    create_ring(&manager, &ring, scratch, response);
    assert(nvx_gsp_channel_ring_destroy(&manager, &ring, scratch,
        sizeof(scratch), response, sizeof(response), 20U, 1U, delay, NULL) ==
        NVX_GSP_RMAPI_TIMEOUT);
    assert(ring.stage == NVX_GSP_CHANNEL_RING_MEMORY_DESTROY_PENDING);
    assert(nvx_gsp_channel_ring_recover(&manager, &ring, scratch,
        sizeof(scratch), response, sizeof(response), 20U, 1U, delay, NULL) ==
        NVX_GSP_RMAPI_OK);
    assert(ring.stage == NVX_GSP_CHANNEL_RING_IDLE);

    return 0;
}
