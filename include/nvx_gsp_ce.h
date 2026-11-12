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
/*
 * DMA copy-engine class IDs (NVIDIA open-gpu-kernel-modules, class/clc5b5.h,
 * clc6b5.h, clc7b5.h). Every method and LAUNCH_DMA field this builder uses has
 * the same offset/layout in all three classes. GA10x chips, including GA106
 * (GeForce RTX 3060), expose AMPERE_DMA_COPY_B on CE0..CE4; allocating any
 * other class would be rejected by the resource manager.
 */
#define NVX_GSP_CE_TURING_DMA_COPY_A_CLASS UINT32_C(0x0000C5B5)
#define NVX_GSP_CE_AMPERE_DMA_COPY_A_CLASS UINT32_C(0x0000C6B5)
#define NVX_GSP_CE_AMPERE_DMA_COPY_B_CLASS UINT32_C(0x0000C7B5)
#define NVX_GSP_CE_GA10X_COPY_CLASS NVX_GSP_CE_AMPERE_DMA_COPY_B_CLASS
#define NVX_GSP_CE_LAUNCH_DMA_FLAGS UINT32_C(0x0000018E)

typedef enum nvx_gsp_ce_status {
    NVX_GSP_CE_OK = 0,
    NVX_GSP_CE_INVALID_ARGUMENT,
    NVX_GSP_CE_NO_SPACE,
    NVX_GSP_CE_BAD_GPU_ADDRESS
} nvx_gsp_ce_status_t;

/*
 * Build a one-dimensional DMA-copy pushbuffer using the public NVC5B5/NVC6B5/
 * NVC7B5 method ABI. It copies bytes from source_gpu_va to destination_gpu_va
 * and asks the CE to write completion_value to completion_gpu_va after the copy.
 *
 * copy_class is the hardware CLASS ID that the first method (SET_OBJECT,
 * NVCLASS field bits 15:0) binds to the subchannel; it is NOT the resource
 * manager handle returned for the allocated object. Only the CE classes listed
 * above are accepted, so passing an RM handle fails with INVALID_ARGUMENT
 * instead of silently emitting a pushbuffer the GPU would reject.
 *
 * The caller must allocate the matching CE class object through the resource
 * manager, map all three addresses in the channel's GPU VA space, make the
 * destination/completion CPU-readable and DMA-coherent (or perform platform
 * cache maintenance), and submit this pushbuffer through a valid Ampere GPFIFO
 * channel.
 */
nvx_gsp_ce_status_t nvx_gsp_ce_build_copy(
    volatile uint32_t *command_buffer, size_t command_capacity_bytes,
    uint32_t copy_class, uint32_t subchannel,
    uint64_t source_gpu_va, uint64_t destination_gpu_va,
    uint64_t completion_gpu_va, uint32_t bytes,
    uint32_t completion_value, size_t *out_command_bytes);

#endif
