/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_CE_H
#define VANTA_NVX_GSP_CE_H

#include <stddef.h>
#include <stdint.h>

#define NVX_GSP_CE_COPY_COMMAND_DWORDS 16U
#define NVX_GSP_CE_COPY_COMMAND_BYTES (NVX_GSP_CE_COPY_COMMAND_DWORDS * 4U)
#define NVX_GSP_CE_GPUVA_LIMIT UINT64_C(0x2000000000000)
#define NVX_GSP_CE_AMPERE_DMA_COPY_CLASS UINT32_C(0x0000C5B5)

typedef enum nvx_gsp_ce_status {
    NVX_GSP_CE_OK = 0,
    NVX_GSP_CE_INVALID_ARGUMENT,
    NVX_GSP_CE_NO_SPACE,
    NVX_GSP_CE_BAD_GPU_ADDRESS
} nvx_gsp_ce_status_t;

/*
 * Build a one-dimensional Ampere DMA-copy pushbuffer using the public NVC5B5
 * method ABI. It copies bytes from source_gpu_va to destination_gpu_va and
 * asks the CE to write completion_value to completion_gpu_va after the copy.
 *
 * The caller must allocate/bind the NVC5B5 object, map all three addresses in
 * the channel's GPU VA space, make the destination/completion CPU-readable,
 * and submit this pushbuffer through a valid Ampere GPFIFO channel.
 */
nvx_gsp_ce_status_t nvx_gsp_ce_build_copy(
    volatile uint32_t *command_buffer, size_t command_capacity_bytes,
    uint32_t copy_engine_object, uint32_t subchannel,
    uint64_t source_gpu_va, uint64_t destination_gpu_va,
    uint64_t completion_gpu_va, uint32_t bytes,
    uint32_t completion_value, size_t *out_command_bytes);

#endif
