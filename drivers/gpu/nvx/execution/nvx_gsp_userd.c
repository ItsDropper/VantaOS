/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_userd.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(nvx_gsp_memory_desc_params_t) == 24U,
    "NV_MEMORY_DESC_PARAMS ABI mismatch");
_Static_assert(sizeof(nvx_gsp_channel_alloc_params_t) == 376U,
    "NV_CHANNEL_ALLOC_PARAMS Ampere ABI size mismatch");
_Static_assert(offsetof(nvx_gsp_channel_alloc_params_t, gpfifo_offset) == 8U,
    "NV_CHANNEL_ALLOC_PARAMS.gpFifoOffset ABI mismatch");
_Static_assert(offsetof(nvx_gsp_channel_alloc_params_t, h_userd_memory) == 36U,
    "NV_CHANNEL_ALLOC_PARAMS.hUserdMemory ABI mismatch");
_Static_assert(offsetof(nvx_gsp_channel_alloc_params_t, userd_offset) == 72U,
    "NV_CHANNEL_ALLOC_PARAMS.userdOffset ABI mismatch");
_Static_assert(offsetof(nvx_gsp_channel_alloc_params_t, engine_type) == 136U,
    "NV_CHANNEL_ALLOC_PARAMS.engineType ABI mismatch");
_Static_assert(offsetof(nvx_gsp_channel_alloc_params_t, instance_mem) == 152U,
    "NV_CHANNEL_ALLOC_PARAMS.instanceMem ABI mismatch");

static int add_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    if (!out || a > UINT64_MAX - b)
        return 0;
    *out = a + b;
    return 1;
}

static int power_of_two(uint32_t value)
{
    return value >= 2U && (value & (value - 1U)) == 0U;
}

nvx_gsp_userd_status_t nvx_gsp_userd_view_init(
    const nvx_gsp_os_memory_t *memory, uint64_t offset,
    size_t required_bytes, nvx_gsp_userd_view_t *out)
{
    uint64_t end;
    uintptr_t cpu;
    if (out)
        *out = (nvx_gsp_userd_view_t){0};
    if (!memory || !out || !required_bytes ||
        required_bytes < NVX_GSP_USERD_MIN_BYTES)
        return NVX_GSP_USERD_INVALID_ARGUMENT;
    if (memory->stage != NVX_GSP_OS_MEMORY_CREATED ||
        !memory->remote_created || !memory->backing.acquired ||
        !memory->memory_handle || !memory->backing.cpu_address ||
        !memory->requested_size)
        return NVX_GSP_USERD_NOT_READY;
    if ((offset & 3U) != 0U)
        return NVX_GSP_USERD_BAD_ALIGNMENT;
    if (!add_u64(offset, (uint64_t)required_bytes, &end) ||
        end > memory->requested_size)
        return NVX_GSP_USERD_OUT_OF_RANGE;
    cpu = (uintptr_t)memory->backing.cpu_address;
    if (offset > UINTPTR_MAX || cpu > UINTPTR_MAX - (uintptr_t)offset)
        return NVX_GSP_USERD_OUT_OF_RANGE;
    cpu += (uintptr_t)offset;
    if (required_bytes > UINTPTR_MAX - cpu)
        return NVX_GSP_USERD_OUT_OF_RANGE;
    if ((cpu & 3U) != 0U)
        return NVX_GSP_USERD_BAD_ALIGNMENT;

    out->base = (volatile uint8_t *)cpu;
    out->gp_get = (volatile uint32_t *)(void *)(out->base +
        NVX_GSP_USERD_GP_GET_OFFSET);
    out->gp_put = (volatile uint32_t *)(void *)(out->base +
        NVX_GSP_USERD_GP_PUT_OFFSET);
    out->bytes = required_bytes;
    return NVX_GSP_USERD_OK;
}

nvx_gsp_userd_status_t nvx_gsp_channel_params_init_ampere(
    nvx_gsp_channel_alloc_params_t *out,
    uint32_t client_handle, uint32_t device_handle,
    uint32_t vaspace_handle, uint32_t channel_id,
    uint32_t engine_type, uint32_t subdevice_mask,
    uint64_t gpfifo_gpu_va, uint32_t gpfifo_entries,
    const nvx_gsp_os_memory_t *userd_memory, uint64_t userd_offset)
{
    uint64_t ring_bytes, ring_end, userd_end;
    if (!out)
        return NVX_GSP_USERD_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!client_handle || !device_handle || !channel_id ||
        client_handle == device_handle || !vaspace_handle ||
        engine_type != NVX_GSP_RM_ENGINE_TYPE_COPY0 ||
        !subdevice_mask || (subdevice_mask & (subdevice_mask - 1U)) != 0U ||
        !power_of_two(gpfifo_entries) ||
        (gpfifo_gpu_va & 7U) != 0U ||
        gpfifo_gpu_va >= NVX_GSP_USERD_GPUVA_LIMIT ||
        !userd_memory || userd_memory->stage != NVX_GSP_OS_MEMORY_CREATED ||
        !userd_memory->remote_created || !userd_memory->backing.acquired ||
        !userd_memory->memory_handle ||
        userd_memory->client_handle != client_handle ||
        userd_memory->device_handle != device_handle ||
        !userd_memory->requested_size ||
        (userd_offset & 3U) != 0U)
        return NVX_GSP_USERD_INVALID_ARGUMENT;

    ring_bytes = (uint64_t)gpfifo_entries * 8U;
    if (!add_u64(gpfifo_gpu_va, ring_bytes, &ring_end) ||
        ring_end > NVX_GSP_USERD_GPUVA_LIMIT)
        return NVX_GSP_USERD_OUT_OF_RANGE;
    if (!add_u64(userd_offset, NVX_GSP_USERD_MIN_BYTES, &userd_end) ||
        userd_end > userd_memory->requested_size)
        return NVX_GSP_USERD_OUT_OF_RANGE;

    out->gpfifo_offset = gpfifo_gpu_va;
    out->gpfifo_entries = gpfifo_entries;
    out->h_vaspace = vaspace_handle;
    out->h_userd_memory[0] = userd_memory->memory_handle;
    out->userd_offset[0] = userd_offset;
    out->engine_type = engine_type;
    out->cid = channel_id;
    out->subdevice_id = subdevice_mask;
    /* hObjectBuffer is documented as no longer used; all descriptors default 0. */
    return NVX_GSP_USERD_OK;
}

nvx_gsp_userd_status_t nvx_gsp_userd_channel_init(
    const nvx_gsp_os_memory_t *userd_memory, uint64_t userd_offset,
    size_t userd_bytes, nvx_gsp_gpfifo_channel_t *channel,
    volatile uint32_t *entries, size_t entries_mapping_bytes,
    volatile uint32_t *doorbell, uint32_t doorbell_token,
    uint32_t entry_count, nvx_gsp_userd_view_t *out_view)
{
    nvx_gsp_userd_status_t status;
    if (!channel || !out_view)
        return NVX_GSP_USERD_INVALID_ARGUMENT;
    status = nvx_gsp_userd_view_init(userd_memory, userd_offset,
        userd_bytes, out_view);
    if (status != NVX_GSP_USERD_OK)
        return status;
    if (nvx_gsp_gpfifo_channel_init(channel, entries, entries_mapping_bytes,
        out_view->gp_get, out_view->gp_put, doorbell, doorbell_token,
        entry_count) != NVX_GSP_GPFIFO_OK) {
        *out_view = (nvx_gsp_userd_view_t){0};
        return NVX_GSP_USERD_INVALID_ARGUMENT;
    }
    return NVX_GSP_USERD_OK;
}
