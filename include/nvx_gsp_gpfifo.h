/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_GPFIFO_H
#define VANTA_NVX_GSP_GPFIFO_H

#include <stddef.h>
#include <stdint.h>
#include "spinlock.h"

#define NVX_GSP_GPFIFO_ENTRY_DWORDS 2U
#define NVX_GSP_GPFIFO_MAX_LENGTH_DWORDS 0x001FFFFFU
#define NVX_GSP_GPFIFO_ADDRESS_LIMIT UINT64_C(0x10000000000)

typedef enum nvx_gsp_gpfifo_status {
    NVX_GSP_GPFIFO_OK = 0,
    NVX_GSP_GPFIFO_INVALID_ARGUMENT,
    NVX_GSP_GPFIFO_NOT_INITIALIZED,
    NVX_GSP_GPFIFO_BAD_RING,
    NVX_GSP_GPFIFO_BAD_REGISTER_STATE,
    NVX_GSP_GPFIFO_QUEUE_FULL,
    NVX_GSP_GPFIFO_BAD_COMMAND_RANGE,
    NVX_GSP_GPFIFO_TIMEOUT
} nvx_gsp_gpfifo_status_t;

typedef void (*nvx_gsp_gpfifo_delay_fn)(uint32_t ms, void *context);

/*
 * Borrowed mappings for an already-created, ABI-compatible NVIDIA GPFIFO
 * channel. entries must map the GPU-visible GPFIFO memory; gp_get/gp_put must
 * address the channel's USERD; doorbell must address the mapped USERMODE
 * notification register; doorbell_token must be the RM-issued channel token.
 * NVX never maps these pointers or guesses their BAR offsets.
 */
typedef struct nvx_gsp_gpfifo_channel {
    volatile uint32_t *entries;
    size_t entries_mapping_bytes;
    volatile uint32_t *gp_get;
    volatile uint32_t *gp_put;
    volatile uint32_t *doorbell;
    uint32_t doorbell_token;
    uint32_t entry_count;
    uint32_t put_index;
    spinlock_t submit_lock;
    uint8_t initialized;
} nvx_gsp_gpfifo_channel_t;

nvx_gsp_gpfifo_status_t nvx_gsp_gpfifo_channel_init(
    nvx_gsp_gpfifo_channel_t *channel, volatile uint32_t *entries,
    size_t entries_mapping_bytes, volatile uint32_t *gp_get,
    volatile uint32_t *gp_put, volatile uint32_t *doorbell,
    uint32_t doorbell_token, uint32_t entry_count);

/*
 * Queue one prebuilt engine command buffer. The GPUVA and dword length are
 * encoded using the public Turing/Ampere GPFIFO entry layout. This only queues
 * work; the caller must use valid engine-specific command packets and a
 * hardware-verified channel/context. USERD and GPFIFO memory must be DMA-coherent
 * (or synchronized by the platform before submission/polling).
 */
nvx_gsp_gpfifo_status_t nvx_gsp_gpfifo_submit(
    nvx_gsp_gpfifo_channel_t *channel, uint64_t command_gpu_va,
    size_t command_bytes, uint32_t *out_put_index);

/*
 * Build the public NVC56F_DMA_NOP method header (zero) in a caller-mapped
 * command buffer, queue it, and wait for GP_GET to consume it. This is a FIFO
 * bring-up probe only; it does not prove an engine operation completed.
 */
nvx_gsp_gpfifo_status_t nvx_gsp_gpfifo_submit_nop_probe(
    nvx_gsp_gpfifo_channel_t *channel, volatile uint32_t *command_cpu_mapping,
    size_t command_mapping_bytes, uint64_t command_gpu_va,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_gpfifo_delay_fn delay_ms, void *context,
    uint32_t *out_put_index);

/*
 * Submit one prebuilt command buffer, wait for fetcher consumption, then wait
 * for its engine-specific completion word. A timeout is not a cancellation.
 */
nvx_gsp_gpfifo_status_t nvx_gsp_gpfifo_submit_and_wait(
    nvx_gsp_gpfifo_channel_t *channel, uint64_t command_gpu_va,
    size_t command_bytes, const volatile uint32_t *completion_word,
    uint32_t expected_completion, uint32_t timeout_ms,
    uint32_t poll_interval_ms, nvx_gsp_gpfifo_delay_fn delay_ms,
    void *context, uint32_t *out_put_index);

/* Wait until USERD GP_GET consumes the submitted GPFIFO entry. */
nvx_gsp_gpfifo_status_t nvx_gsp_gpfifo_wait_get(
    nvx_gsp_gpfifo_channel_t *channel, uint32_t expected_get_index,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_gpfifo_delay_fn delay_ms, void *context);

/*
 * Wait for a completion word written by a GPU command stream (for example, an
 * engine-specific semaphore release). GP_GET alone does not prove execution
 * completed; this separate wait is required to verify a workload result.
 */
nvx_gsp_gpfifo_status_t nvx_gsp_gpfifo_wait_completion(
    const volatile uint32_t *completion_word, uint32_t expected_value,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_gpfifo_delay_fn delay_ms, void *context);

#endif
