/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_msgq_ga10x.h"
#include "nvx_gsp_msgq.h"
#include "nvx_gsp_gmcapi.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TEST_QUEUE_BYTES UINT64_C(0x40000)
#define TEST_SHARED_BYTES (NVX_GSP_PAGE_SIZE + 2U * TEST_QUEUE_BYTES)
#define TEST_BAR_BYTES 0x120000U

typedef struct fake_gsp {
    nvx_gsp_msgq_ga10x_t *runtime;
    const nvx_mmio_region_t *bar;
    uint32_t elapsed;
    uint8_t responded;
} fake_gsp_t;

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}
static uint64_t get64(const uint8_t *p)
{
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4U) << 32U);
}
static void put32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8U);
    p[2]=(uint8_t)(v>>16U); p[3]=(uint8_t)(v>>24U);
}
static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v); put32(p + 4U, (uint32_t)(v >> 32U));
}
static void fake_gsp_reply(void *context, uint32_t ms)
{
    fake_gsp_t *fake = (fake_gsp_t *)context;
    fake->elapsed += ms;
    if (fake->responded) return;

    const uint8_t *request = fake->runtime->command_queue.layout.entries;
    uint8_t *reply = fake->runtime->status_queue.layout.entries;
    const uint64_t sequence = get64(request + 32U);
    const uint32_t command = get32(request + 24U);
    const uint64_t static_info = UINT64_C(0x1122334455667788);
    const uint32_t payload_size = (uint32_t)sizeof(static_info);
    const uint32_t gmc_size = NVX_GSP_GMCAPI_HEADER_SIZE + payload_size;
    const uint32_t wire_size = 24U + gmc_size;

    memset(reply, 0, NVX_GSP_PAGE_SIZE);
    put32(reply, NVX_GSP_MCTP_MAGIC);
    put32(reply + 4U, wire_size);
    put32(reply + 8U, NVX_GSP_MCTP_HEADER_SINGLE);
    put32(reply + 12U, NVX_GSP_GMCAPI_NVDM_HEADER);
    put32(reply + 16U, gmc_size);
    put32(reply + 20U, 0U);
    put32(reply + 24U, command | NVX_GSP_GMCAPI_COMMAND_FLAG_RESPONSE);
    put32(reply + 28U, payload_size);
    put64(reply + 32U, sequence);
    put32(reply + 40U, 0U);
    memcpy(reply + 64U, &static_info, sizeof(static_info));
    assert(nvx_mmio_write32(fake->bar, NVX_GSP_GA10X_MSGQ_HEAD(0U),
        fake->runtime->status_queue.local_counter + 1U) == NVX_MMIO_OK);
    fake->responded = 1U;
}

static uint32_t read_reg(const nvx_mmio_region_t *bar, uint32_t offset)
{
    uint32_t value = UINT32_MAX;
    assert(nvx_mmio_read32(bar, offset, &value) == NVX_MMIO_OK);
    return value;
}

int main(void)
{
    uint8_t *bar_bytes = calloc(1U, TEST_BAR_BYTES);
    uint8_t *shared_bytes = aligned_alloc(NVX_GSP_PAGE_SIZE,
                                          (size_t)TEST_SHARED_BYTES);
    nvx_mmio_region_t bar = {bar_bytes, TEST_BAR_BYTES};
    nvx_dma_buffer_t shared = {
        .cpu_address = shared_bytes,
        .gpu_address = UINT64_C(0x60000000),
        .size = (size_t)TEST_SHARED_BYTES,
        .allocated_size = (size_t)TEST_SHARED_BYTES,
        .page_count = (size_t)(TEST_SHARED_BYTES / NVX_GSP_PAGE_SIZE),
        .allocation_id = 77U,
        .acquired = 1U
    };
    nvx_dma_sync_t sync = {0};
    nvx_gsp_modern_shared_queue_config_t config = {0};
    nvx_gsp_modern_shared_queue_layout_t layout = {0};
    nvx_gsp_msgq_ga10x_t runtime = {0};
    fake_gsp_t fake = {0};
    nvx_gsp_gmcapi_transport_t gmcapi = {0};
    uint8_t request[8] = {1U,2U,3U,4U,5U,6U,7U,8U};
    uint8_t response[64] = {0};
    uint8_t *scratch = aligned_alloc(NVX_GSP_PAGE_SIZE, NVX_GSP_PAGE_SIZE);
    size_t response_size = 0U;
    uint32_t remote_status = UINT32_MAX;

    assert(bar_bytes && shared_bytes && scratch);
    memset(scratch, 0, NVX_GSP_PAGE_SIZE);
    memset(shared_bytes, 0, (size_t)TEST_SHARED_BYTES);
    config.shared_mem_phys_addr = shared.gpu_address;
    config.allocation_size = shared.size;
    config.command_queue_size = TEST_QUEUE_BYTES;
    config.status_queue_size = TEST_QUEUE_BYTES;
    config.queue_element_header_size = NVX_GSP_QUEUE_HEADER_SIZE_PLAIN;
    config.queue_element_size_min = NVX_GSP_QUEUE_ELEMENT_MIN_SIZE;
    config.queue_element_size_max = NVX_GSP_QUEUE_ELEMENT_MAX_SIZE;
    config.queue_header_alignment = NVX_GSP_QUEUE_HEADER_ALIGNMENT;
    config.queue_element_alignment = NVX_GSP_QUEUE_ELEMENT_ALIGNMENT;
    assert(nvx_gsp_modern_shared_queue_plan(&config, &layout) ==
           NVX_GSP_INIT_ARGS_OK);
    assert(layout.required_size == TEST_SHARED_BYTES);
    assert(nvx_gsp_modern_shared_queue_build_pte_table(
        &layout, shared_bytes, shared.size) == NVX_GSP_INIT_ARGS_OK);

    assert(nvx_dma_sync_init(&sync, &shared) == NVX_DMA_SYNC_OK);
    assert(nvx_gsp_msgq_ga10x_prepare_command(
        &runtime, &shared, &layout, &bar) == NVX_GSP_MSGQ_GA10X_OK);
    assert(read_reg(&bar, NVX_GSP_GA10X_QUEUE_HEAD(0U)) == 0U);
    assert(runtime.command_queue.initialized == 1U);
    assert(runtime.command_queue.transmit == 1U);

    /* Model the GSP-created TX header for its status queue. */
    assert(nvx_gsp_msgq_prepare_tx(
        shared_bytes + layout.status_queue_offset,
        (size_t)layout.status_queue_size, NVX_GSP_PAGE_SIZE,
        &(nvx_gsp_msgq_layout_t){0}) == NVX_GSP_MSGQ_OK);
    assert(nvx_dma_sync_for_shared(&sync, 0U, shared.size) == NVX_DMA_SYNC_OK);
    assert(sync.owner == NVX_DMA_OWNER_SHARED);

    assert(nvx_gsp_msgq_ga10x_link_status(
        &runtime, &shared, &sync, &layout, 20U, 2U,
        fake_gsp_reply, &(fake_gsp_t){0}) == NVX_GSP_MSGQ_GA10X_OK);
    assert(runtime.status_linked == 1U);
    assert(runtime.status_queue.initialized == 1U);
    assert(runtime.status_queue.transmit == 0U);
    assert(read_reg(&bar, NVX_GSP_GA10X_MSGQ_TAIL(0U)) == 0U);
    fake.runtime = &runtime;
    fake.bar = &bar;
    assert(nvx_gsp_gmcapi_init(&gmcapi, &runtime.command_queue,
                              &runtime.status_queue) == NVX_GSP_GMCAPI_OK);
    assert(nvx_gsp_gmcapi_gsp_init(
        &gmcapi, request, sizeof(request), response, sizeof(response),
        &response_size, &remote_status, scratch, NVX_GSP_PAGE_SIZE,
        20U, 1U, fake_gsp_reply, &fake) == NVX_GSP_GMCAPI_OK);
    assert(fake.responded == 1U);
    assert(fake.elapsed == 1U);
    assert(response_size == sizeof(uint64_t));
    assert(remote_status == 0U);
    assert(get64(response) == UINT64_C(0x1122334455667788));
    assert(get32(runtime.command_queue.layout.entries + 24U) ==
           NVX_GSP_GMCAPI_CMD_GSP_INIT);
    assert(get32(runtime.command_queue.layout.entries + 12U) ==
           NVX_GSP_GMCAPI_NVDM_HEADER);
    assert(get32(runtime.command_queue.layout.entries + 28U) ==
           sizeof(request));
    assert(get32(runtime.command_queue.layout.entries + 40U) ==
           sizeof(response));

    /* Shared ownership is returned only after the caller quiesces GSP. */
    assert(nvx_dma_sync_for_cpu(&sync, 0U, shared.size) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_destroy(&sync) == NVX_DMA_SYNC_OK);
    free(scratch);
    free(shared_bytes);
    free(bar_bytes);
    return 0;
}
