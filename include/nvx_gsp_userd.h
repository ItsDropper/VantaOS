/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_USERD_H
#define VANTA_NVX_GSP_USERD_H

#include "nvx_gsp_channel.h"
#include "nvx_gsp_gpfifo.h"
#include "nvx_gsp_os_memory.h"

/*
 * Public Turing/Ampere RAMUSERD offsets from NVIDIA open-gpu-doc
 * manuals/turing/tu104/dev_ram.ref.txt. USERD is a 0x100-byte control block;
 * GP_GET is GPU-owned/read-only and GP_PUT is host-owned/writable.
 */
#define NVX_GSP_USERD_GP_GET_OFFSET UINT32_C(0x88)
#define NVX_GSP_USERD_GP_PUT_OFFSET UINT32_C(0x8C)
#define NVX_GSP_USERD_MIN_BYTES UINT32_C(0x90)
#define NVX_GSP_USERD_GPUVA_LIMIT UINT64_C(0x10000000000)

typedef enum nvx_gsp_userd_status {
    NVX_GSP_USERD_OK = 0,
    NVX_GSP_USERD_INVALID_ARGUMENT,
    NVX_GSP_USERD_NOT_READY,
    NVX_GSP_USERD_OUT_OF_RANGE,
    NVX_GSP_USERD_BAD_ALIGNMENT
} nvx_gsp_userd_status_t;

typedef struct nvx_gsp_userd_view {
    volatile uint32_t *gp_get;
    volatile uint32_t *gp_put;
    volatile uint8_t *base;
    size_t bytes;
} nvx_gsp_userd_view_t;

/* Derive CPU-visible GP_GET/GP_PUT from an imported RM OS-descriptor object. */
nvx_gsp_userd_status_t nvx_gsp_userd_view_init(
    const nvx_gsp_os_memory_t *memory, uint64_t offset,
    size_t required_bytes, nvx_gsp_userd_view_t *out);

/*
 * Build the R535+ 368-byte NV_CHANNEL_ALLOC_PARAMS for an Ampere GPFIFO
 * channel. gpFifoOffset is the GPU virtual address of the ring (not a CPU
 * pointer); hObjectBuffer is obsolete and remains zero. USERD is supplied
 * through hUserdMemory[0]/userdOffset[0], so userdMem remains zero.
 */
nvx_gsp_userd_status_t nvx_gsp_channel_params_init_ampere(
    nvx_gsp_channel_alloc_params_t *out,
    uint32_t client_handle, uint32_t device_handle,
    uint32_t vaspace_handle, uint32_t channel_id,
    uint32_t engine_type, uint32_t subdevice_mask,
    uint64_t gpfifo_gpu_va, uint32_t gpfifo_entries,
    const nvx_gsp_os_memory_t *userd_memory, uint64_t userd_offset);

/* Initialize the host-side GPFIFO view from the validated USERD mapping. */
nvx_gsp_userd_status_t nvx_gsp_userd_channel_init(
    const nvx_gsp_os_memory_t *userd_memory, uint64_t userd_offset,
    size_t userd_bytes, nvx_gsp_gpfifo_channel_t *channel,
    volatile uint32_t *entries, size_t entries_mapping_bytes,
    volatile uint32_t *doorbell, uint32_t doorbell_token,
    uint32_t entry_count, nvx_gsp_userd_view_t *out_view);

#endif
