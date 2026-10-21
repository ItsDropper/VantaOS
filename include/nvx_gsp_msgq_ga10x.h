/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_MSGQ_GA10X_H
#define VANTA_NVX_GSP_MSGQ_GA10X_H

#include "nvx_dma_sync.h"
#include "nvx_gsp_init_args.h"
#include "nvx_gsp_msgq_queue.h"

/*
 * GA10x inherits the PGSP queue register map documented in NVIDIA's public
 * TU102 dev_gsp.h. Queue index 0 is the RM command/status channel.
 */
#define NVX_GSP_GA10X_RM_QUEUE_INDEX 0U
#define NVX_GSP_GA10X_QUEUE_COUNT 8U
#define NVX_GSP_GA10X_QUEUE_HEAD(i) (0x110C00U + (uint32_t)(i) * 8U)
#define NVX_GSP_GA10X_QUEUE_TAIL(i) (0x110C04U + (uint32_t)(i) * 8U)
#define NVX_GSP_GA10X_MSGQ_HEAD(i)  (0x110C80U + (uint32_t)(i) * 8U)
#define NVX_GSP_GA10X_MSGQ_TAIL(i)  (0x110C84U + (uint32_t)(i) * 8U)

typedef enum nvx_gsp_msgq_ga10x_status {
    NVX_GSP_MSGQ_GA10X_OK = 0,
    NVX_GSP_MSGQ_GA10X_INVALID_ARGUMENT,
    NVX_GSP_MSGQ_GA10X_BAD_LAYOUT,
    NVX_GSP_MSGQ_GA10X_NOT_SHARED,
    NVX_GSP_MSGQ_GA10X_TIMEOUT,
    NVX_GSP_MSGQ_GA10X_IO_ERROR
} nvx_gsp_msgq_ga10x_status_t;

typedef void (*nvx_gsp_msgq_ga10x_delay_fn)(void *context, uint32_t ms);

typedef struct nvx_gsp_msgq_ga10x {
    nvx_gsp_msgq_mmio_context_t mmio_context;
    nvx_gsp_msgq_io_t io;
    nvx_gsp_msgq_queue_t command_queue;
    nvx_gsp_msgq_queue_t status_queue;
    uint8_t command_initialized;
    uint8_t status_linked;
} nvx_gsp_msgq_ga10x_t;

/*
 * Call before transferring shared memory to GSP. Initializes the CPU-producer
 * command queue and its documented hardware head/tail registers.
 */
nvx_gsp_msgq_ga10x_status_t nvx_gsp_msgq_ga10x_prepare_command(
    nvx_gsp_msgq_ga10x_t *runtime, const nvx_dma_buffer_t *shared_memory,
    const nvx_gsp_modern_shared_queue_layout_t *layout,
    const nvx_mmio_region_t *bar0);

/*
 * Call only after GSP RISC-V is active and the shared allocation is in
 * NVX_DMA_OWNER_SHARED state. Waits for GSP's status-queue header, then links
 * the CPU-consumer queue by publishing its tail register. Queue memory must
 * remain coherent and shared for the lifetime of the runtime.
 */
nvx_gsp_msgq_ga10x_status_t nvx_gsp_msgq_ga10x_link_status(
    nvx_gsp_msgq_ga10x_t *runtime, const nvx_dma_buffer_t *shared_memory,
    const nvx_dma_sync_t *shared_sync,
    const nvx_gsp_modern_shared_queue_layout_t *layout,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_msgq_ga10x_delay_fn delay_ms, void *context);

#endif
