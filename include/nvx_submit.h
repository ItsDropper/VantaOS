/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_SUBMIT_H
#define VANTA_NVX_SUBMIT_H

#include <stddef.h>
#include <stdint.h>
#include "nvx_object_memory.h"
#include "nvx_sync.h"

#define NVX_SUBMIT_MAX_RELOCATIONS 1024U
#define NVX_SUBMIT_MAX_COMMAND_BYTES NVX_DMA_MAX_BYTES

typedef enum nvx_relocation_width {
    NVX_RELOC_ADDRESS32 = 4,
    NVX_RELOC_ADDRESS64 = 8
} nvx_relocation_width_t;

typedef enum nvx_relocation_access {
    NVX_RELOC_READ = 1,
    NVX_RELOC_WRITE = 2,
    NVX_RELOC_READ_WRITE = 3
} nvx_relocation_access_t;

typedef struct nvx_relocation {
    uint32_t command_offset;
    uint32_t target_offset;
    uint32_t target_span;
    uint8_t width;
    uint8_t access;
    nvx_memory_handle_t target;
} nvx_relocation_t;

typedef enum nvx_submit_status {
    NVX_SUBMIT_OK = 0,
    NVX_SUBMIT_INVALID_ARGUMENT,
    NVX_SUBMIT_INVALID_COMMAND_RANGE,
    NVX_SUBMIT_TOO_MANY_RELOCATIONS,
    NVX_SUBMIT_INVALID_RELOCATION,
    NVX_SUBMIT_RELOCATION_OVERLAP,
    NVX_SUBMIT_TARGET_INVALID,
    NVX_SUBMIT_ADDRESS_OVERFLOW,
    NVX_SUBMIT_ADDRESS_WIDTH,
    NVX_SUBMIT_MEMORY_ERROR,
    NVX_SUBMIT_FENCE_PENDING,
    NVX_SUBMIT_DEVICE_LOST,
    NVX_SUBMIT_STALE_FENCE
} nvx_submit_status_t;

typedef struct nvx_submit_report {
    uint32_t relocation_count;
    uint32_t read_relocation_count;
    uint32_t write_relocation_count;
    uint32_t address32_count;
    uint32_t address64_count;
    uint64_t command_gpu_address;
    uint64_t command_bytes;
} nvx_submit_report_t;

#define NVX_SUBMIT_MAX_PINNED_OBJECTS (NVX_SUBMIT_MAX_RELOCATIONS + 1U)

/*
 * Caller-owned, long-lived lease. It pins the command buffer and every unique
 * relocation target until release_batch() is called after GPU completion or
 * verified engine quiescence. The lease is intentionally large; do not put it
 * on a small kernel stack.
 */
typedef struct nvx_submit_lease {
    nvx_memory_handle_t handles[NVX_SUBMIT_MAX_PINNED_OBJECTS];
    uint32_t count;
    uint32_t release_index;
    uint8_t active;
} nvx_submit_lease_t;

/*
 * Validates an opaque command buffer and its relocation metadata. NVIDIA
 * opcodes are intentionally not decoded or patched here: engine-specific
 * packet formats must be sourced and implemented before real submission.
 */
nvx_submit_status_t nvx_submit_validate_batch(
    nvx_memory_manager_t *memory,
    const nvx_memory_handle_t *command_buffer,
    size_t command_bytes,
    const nvx_relocation_t *relocations,
    size_t relocation_count,
    nvx_submit_report_t *out_report);

/*
 * Pin the command buffer and every unique relocation target. A partial
 * acquisition that cannot be rolled back leaves lease.active set; release it.
 */
nvx_submit_status_t nvx_submit_acquire_batch(
    nvx_memory_manager_t *memory, const nvx_memory_handle_t *command_buffer,
    size_t command_bytes, const nvx_relocation_t *relocations,
    size_t relocation_count, nvx_submit_lease_t *lease,
    nvx_submit_report_t *out_report);

/* Release pins in reverse order; retry after an unpin error. */
nvx_submit_status_t nvx_submit_release_batch(
    nvx_memory_manager_t *memory, nvx_submit_lease_t *lease);

/*
 * Release only after a trusted completion source signals the fence.
 * Pending/failed/stale fences retain all pins. Device-loss cleanup requires
 * separate proof that the engine can no longer access the memory.
 */
nvx_submit_status_t nvx_submit_release_after_fence(
    nvx_memory_manager_t *memory, nvx_submit_lease_t *lease,
    nvx_sync_timeline_t *timeline, const nvx_fence_t *fence);

#endif
