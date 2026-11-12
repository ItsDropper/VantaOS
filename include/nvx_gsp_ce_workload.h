/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_CE_WORKLOAD_H
#define VANTA_NVX_GSP_CE_WORKLOAD_H
#include "nvx_dma_sync.h"
#include "nvx_gsp_ce.h"
#include "nvx_object_memory.h"

typedef enum nvx_gsp_ce_workload_status {
    NVX_GSP_CE_WORKLOAD_OK = 0,
    NVX_GSP_CE_WORKLOAD_INVALID_ARGUMENT,
    NVX_GSP_CE_WORKLOAD_MEMORY_ERROR,
    NVX_GSP_CE_WORKLOAD_SYNC_ERROR,
    NVX_GSP_CE_WORKLOAD_BUILD_ERROR,
    NVX_GSP_CE_WORKLOAD_SUBMIT_ERROR,
    NVX_GSP_CE_WORKLOAD_IN_FLIGHT,
    NVX_GSP_CE_WORKLOAD_TIMEOUT,
    NVX_GSP_CE_WORKLOAD_COMPLETION_ERROR
} nvx_gsp_ce_workload_status_t;

typedef enum nvx_gsp_ce_workload_stage {
    NVX_GSP_CE_WORKLOAD_IDLE = 0,
    NVX_GSP_CE_WORKLOAD_PENDING,
    NVX_GSP_CE_WORKLOAD_RELEASE_PENDING,
    NVX_GSP_CE_WORKLOAD_CLEANUP_PENDING
} nvx_gsp_ce_workload_stage_t;

/* Caller-owned lease; keep it alive and do not copy it across an in-flight GPU submission. */
typedef struct nvx_gsp_ce_workload {
    nvx_memory_manager_t *memory;
    nvx_memory_handle_t handles[4]; /* command, source, destination, completion */
    nvx_dma_sync_t sync[4];
    volatile uint32_t *completion_word;
    uint32_t completion_value;
    uint32_t put_index;
    uint32_t pinned_count;
    uint8_t stage;
} nvx_gsp_ce_workload_t;

/*
 * Execute one bounded copy with four distinct GPUVM allocations. Required
 * permissions: command READ, source READ, destination WRITE, completion WRITE.
 * The helper pins allocations and transfers DMA ownership until the GPU
 * completion semaphore is observed. A timeout is not cancellation.
 */
nvx_gsp_ce_workload_status_t nvx_gsp_ce_copy_start(
    nvx_gsp_ce_workload_t *workload, nvx_memory_manager_t *memory,
    nvx_gsp_gpfifo_channel_t *channel,
    const nvx_memory_handle_t *command,
    const nvx_memory_handle_t *source,
    const nvx_memory_handle_t *destination,
    const nvx_memory_handle_t *completion,
    uint32_t copy_class, uint32_t subchannel, uint32_t bytes,
    uint32_t completion_value, uint32_t timeout_ms,
    uint32_t poll_interval_ms, nvx_gsp_gpfifo_delay_fn delay_ms,
    void *delay_context, nvx_gsp_gpfifo_status_t *out_fifo_status);

/* Wait for GPU completion, restore CPU ownership and release all four pins. */
nvx_gsp_ce_workload_status_t nvx_gsp_ce_copy_recover(
    nvx_gsp_ce_workload_t *workload, uint32_t timeout_ms,
    uint32_t poll_interval_ms, nvx_gsp_gpfifo_delay_fn delay_ms,
    void *delay_context);
#endif
