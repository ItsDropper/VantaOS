/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_msgq_ga10x.h"
#include "nvx_gsp_msgq.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TEST_QUEUE_BYTES UINT64_C(0x40000)
#define TEST_SHARED_BYTES (NVX_GSP_PAGE_SIZE + 2U * TEST_QUEUE_BYTES)
#define TEST_BAR_BYTES 0x120000U

static void no_delay(void *context, uint32_t ms)
{
    uint32_t *elapsed = (uint32_t *)context;
    *elapsed += ms;
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
    uint32_t elapsed = 0U;

    assert(bar_bytes && shared_bytes);
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
        no_delay, &elapsed) == NVX_GSP_MSGQ_GA10X_OK);
    assert(runtime.status_linked == 1U);
    assert(runtime.status_queue.initialized == 1U);
    assert(runtime.status_queue.transmit == 0U);
    assert(read_reg(&bar, NVX_GSP_GA10X_MSGQ_TAIL(0U)) == 0U);
    assert(elapsed == 0U);

    /* Shared ownership is returned only after the caller quiesces GSP. */
    assert(nvx_dma_sync_for_cpu(&sync, 0U, shared.size) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_destroy(&sync) == NVX_DMA_SYNC_OK);
    free(shared_bytes);
    free(bar_bytes);
    return 0;
}
