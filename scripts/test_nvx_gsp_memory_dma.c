/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_memory_dma.h"

#include <assert.h>
#include <string.h>

static nvx_gsp_map_memory_dma_t captured;
static unsigned map_calls, unmap_calls, recover_calls;
static nvx_gsp_rmapi_status_t map_result = NVX_GSP_RMAPI_OK;
static nvx_gsp_rmapi_status_t unmap_result = NVX_GSP_RMAPI_OK;
static nvx_gsp_rmapi_status_t recover_result = NVX_GSP_RMAPI_OK;

nvx_gsp_rmapi_status_t nvx_gsp_memory_object_get_range(
    const nvx_gsp_memory_object_t *object, uint64_t *size, uint64_t *limit)
{
    if (!object || !size || !limit || !object->result_valid)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    *size = object->actual_size;
    *limit = object->limit;
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_dma_map(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_dma_mapping_t *mapping,
    const nvx_gsp_map_memory_dma_t *request,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    (void)manager; (void)scratch; (void)scratch_size; (void)response;
    (void)response_capacity; (void)timeout_ms; (void)poll_interval_ms;
    (void)delay_ms; (void)delay_context;
    captured = *request;
    ++map_calls;
    mapping->request = *request;
    mapping->stage = NVX_GSP_DMA_MAP_MAPPED;
    mapping->dma_offset = request->dma_offset ? request->dma_offset : 0x400000U;
    return map_result;
}

nvx_gsp_rmapi_status_t nvx_gsp_dma_map_recover(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_dma_mapping_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    (void)manager; (void)mapping; (void)scratch; (void)scratch_size;
    (void)response; (void)response_capacity; (void)timeout_ms;
    (void)poll_interval_ms; (void)delay_ms; (void)delay_context;
    ++recover_calls;
    return recover_result;
}

nvx_gsp_rmapi_status_t nvx_gsp_dma_unmap(
    nvx_gsp_rpc_manager_t *manager, nvx_gsp_dma_mapping_t *mapping,
    void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context)
{
    (void)manager; (void)mapping; (void)scratch; (void)scratch_size;
    (void)response; (void)response_capacity; (void)timeout_ms;
    (void)poll_interval_ms; (void)delay_ms; (void)delay_context;
    ++unmap_calls;
    return unmap_result;
}

static void delay(uint32_t ms, void *context) { (void)ms; (void)context; }

int main(void)
{
    nvx_gsp_rpc_manager_t manager = {0};
    nvx_gsp_memory_object_t memory = {
        .client_handle = 1U, .parent_handle = 2U, .memory_handle = 3U,
        .actual_size = 0x20000U, .limit = 0x1FFFFU,
        .remote_created = 1U, .result_valid = 1U,
        .stage = NVX_GSP_MEMORY_CREATED
    };
    nvx_gsp_context_dma_t context = {
        .client_handle = 1U, .device_handle = 2U, .context_dma_handle = 4U,
        .params = {.h_memory = 3U, .offset = 0x1000U, .limit = 0xFFFFU},
        .stage = NVX_GSP_CONTEXT_DMA_CREATED
    };
    nvx_gsp_dma_mapping_t mapping = {0};
    uint8_t scratch[128], response[128];

    assert(nvx_gsp_memory_dma_map(&manager, &memory, &context, &mapping,
        0x2000U, 0x3000U, 0x11U, 0x22U, 0x33U, 0U,
        scratch, sizeof(scratch), response, sizeof(response),
        100U, 1U, delay, NULL) == NVX_GSP_RMAPI_OK);
    assert(map_calls == 1U && mapping.stage == NVX_GSP_DMA_MAP_MAPPED);
    assert(captured.client == 1U && captured.device == 2U &&
        captured.dma == 4U && captured.memory == 3U);
    assert(captured.offset == 0x2000U && captured.length == 0x3000U);
    assert(captured.flags == 0x11U && captured.flags2 == 0x22U &&
        captured.kind_override == 0x33U);
    assert(mapping.dma_offset == 0x400000U);

    assert(nvx_gsp_memory_dma_unmap(&manager, &memory, &context, &mapping,
        scratch, sizeof(scratch), response, sizeof(response),
        100U, 1U, delay, NULL) == NVX_GSP_RMAPI_OK);
    assert(unmap_calls == 1U);

    assert(nvx_gsp_memory_dma_recover(&manager, &memory, &context, &mapping,
        scratch, sizeof(scratch), response, sizeof(response),
        100U, 1U, delay, NULL) == NVX_GSP_RMAPI_OK);
    assert(recover_calls == 1U);

    unsigned before = map_calls;
    assert(nvx_gsp_memory_dma_map(&manager, &memory, &context, &mapping,
        0x10000U, 0x10000U, 0U, 0U, 0U, 0U,
        scratch, sizeof(scratch), response, sizeof(response),
        100U, 1U, delay, NULL) == NVX_GSP_RMAPI_INVALID_ARGUMENT);
    assert(map_calls == before);

    context.params.h_memory = 99U;
    assert(nvx_gsp_memory_dma_map(&manager, &memory, &context, &mapping,
        0x2000U, 0x1000U, 0U, 0U, 0U, 0U,
        scratch, sizeof(scratch), response, sizeof(response),
        100U, 1U, delay, NULL) == NVX_GSP_RMAPI_INVALID_ARGUMENT);
    context.params.h_memory = 3U;
    memory.result_valid = 0U;
    assert(nvx_gsp_memory_dma_map(&manager, &memory, &context, &mapping,
        0x2000U, 0x1000U, 0U, 0U, 0U, 0U,
        scratch, sizeof(scratch), response, sizeof(response),
        100U, 1U, delay, NULL) == NVX_GSP_RMAPI_INVALID_ARGUMENT);
    return 0;
}
