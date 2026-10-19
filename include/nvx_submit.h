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
#include "nvx_memory.h"

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
    NVX_SUBMIT_MEMORY_ERROR
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

#endif
