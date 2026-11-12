/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_userd.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    uint8_t backing[0x5000] = {0};
    nvx_gsp_os_memory_t memory = {
        .client_handle = 0x10U,
        .device_handle = 0x20U,
        .memory_handle = 0x30U,
        .requested_size = sizeof(backing),
        .backing = {
            .cpu_address = backing,
            .gpu_address = 0x200000U,
            .size = sizeof(backing),
            .allocated_size = sizeof(backing),
            .page_count = 5U,
            .address_bits = 30U,
            .acquired = 1U
        },
        .remote_created = 1U,
        .stage = NVX_GSP_OS_MEMORY_CREATED
    };
    nvx_gsp_userd_view_t view = {0};
    nvx_gsp_channel_alloc_params_t params;
    nvx_gsp_gpfifo_channel_t channel = {0};
    volatile uint32_t entries[8] = {0};
    volatile uint32_t doorbell = 0U;
    const uint64_t ring_va = UINT64_C(0x12340000);

    assert(sizeof(nvx_gsp_channel_alloc_params_t) == 368U);
    assert(offsetof(nvx_gsp_channel_alloc_params_t, h_userd_memory) == 32U);
    assert(offsetof(nvx_gsp_channel_alloc_params_t, userd_offset) == 64U);
    assert(nvx_gsp_userd_view_init(&memory, 0x1000U, 0x100U, &view) ==
        NVX_GSP_USERD_OK);
    assert(view.base == backing + 0x1000U);
    assert(view.gp_get == (volatile uint32_t *)(void *)(backing + 0x1088U));
    assert(view.gp_put == (volatile uint32_t *)(void *)(backing + 0x108CU));
    assert(view.bytes == 0x100U);

    assert(nvx_gsp_channel_params_init_ampere(&params, 0x10U, 0x20U,
        0x40U, 0x50U, NVX_GSP_RM_ENGINE_TYPE_COPY0, 1U,
        ring_va, 256U, &memory, 0x1000U) == NVX_GSP_USERD_OK);
    assert(params.h_object_error == 0U);
    assert(params.h_object_buffer == 0U);
    assert(params.gpfifo_offset == ring_va);
    assert(params.gpfifo_entries == 256U);
    assert(params.h_vaspace == 0x40U);
    assert(params.h_userd_memory[0] == 0x30U);
    for (size_t i = 1U; i < NVX_GSP_RM_MAX_SUBDEVICES; ++i)
        assert(params.h_userd_memory[i] == 0U);
    assert(params.userd_offset[0] == 0x1000U);
    assert(params.engine_type == NVX_GSP_RM_ENGINE_TYPE_COPY0);
    assert(params.cid == 0x50U);
    assert(params.subdevice_id == 1U);
    assert(params.userd_mem.base == 0U);

    /* Derive the GPFIFO host pointers from USERD rather than guessed offsets. */
    assert(nvx_gsp_userd_channel_init(&memory, 0x1000U, 0x100U,
        &channel, entries, sizeof(entries), &doorbell, 0xA55AU, 4U,
        &view) == NVX_GSP_USERD_OK);
    assert(channel.initialized);
    assert(channel.gp_get == view.gp_get && channel.gp_put == view.gp_put);
    assert(channel.entry_count == 4U);

    assert(nvx_gsp_userd_view_init(&memory, 0x4F80U, 0x100U, &view) ==
        NVX_GSP_USERD_OUT_OF_RANGE);
    assert(view.base == NULL && view.gp_get == NULL && view.gp_put == NULL);
    assert(nvx_gsp_userd_view_init(&memory, 3U, 0x100U, &view) ==
        NVX_GSP_USERD_BAD_ALIGNMENT);
    assert(nvx_gsp_userd_view_init(&memory, 0U, 0x8CU, &view) ==
        NVX_GSP_USERD_INVALID_ARGUMENT);

    assert(nvx_gsp_channel_params_init_ampere(&params, 0x10U, 0x20U,
        0x40U, 0x50U, NVX_GSP_RM_ENGINE_TYPE_COPY0, 1U,
        ring_va, 3U, &memory, 0x1000U) == NVX_GSP_USERD_INVALID_ARGUMENT);
    assert(nvx_gsp_channel_params_init_ampere(&params, 0x10U, 0x20U,
        0x40U, 0x50U, NVX_GSP_RM_ENGINE_TYPE_COPY0, 1U,
        NVX_GSP_USERD_GPUVA_LIMIT - 0x100U, 256U, &memory, 0x1000U) ==
        NVX_GSP_USERD_OUT_OF_RANGE);
    assert(nvx_gsp_channel_params_init_ampere(&params, 0x10U, 0x20U,
        0x40U, 0x50U, NVX_GSP_RM_ENGINE_TYPE_COPY0, 3U,
        ring_va, 256U, &memory, 0x1000U) == NVX_GSP_USERD_INVALID_ARGUMENT);
    memory.client_handle = 0x99U;
    assert(nvx_gsp_channel_params_init_ampere(&params, 0x10U, 0x20U,
        0x40U, 0x50U, NVX_GSP_RM_ENGINE_TYPE_COPY0, 1U,
        ring_va, 256U, &memory, 0x1000U) == NVX_GSP_USERD_INVALID_ARGUMENT);
    memory.client_handle = 0x10U;
    memory.stage = NVX_GSP_OS_MEMORY_FREE_PENDING;
    assert(nvx_gsp_userd_view_init(&memory, 0x1000U, 0x100U, &view) ==
        NVX_GSP_USERD_NOT_READY);
    return 0;
}
