/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_OBJECT_MEMORY_H
#define VANTA_NVX_OBJECT_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include "nvx_dma.h"
#include "nvx_gpuva.h"
#include "spinlock.h"

#define NVX_MEMORY_MAX_OBJECTS 64U

typedef enum nvx_memory_status {
    NVX_MEMORY_OK = 0, NVX_MEMORY_INVALID_ARGUMENT, NVX_MEMORY_NOT_INITIALIZED,
    NVX_MEMORY_OUT_OF_OBJECTS, NVX_MEMORY_ALLOCATION_FAILED,
    NVX_MEMORY_ADDRESS_SPACE_FAILED, NVX_MEMORY_STALE_HANDLE,
    NVX_MEMORY_BUSY, NVX_MEMORY_CORRUPT_STATE
} nvx_memory_status_t;

typedef enum nvx_memory_kind {
    NVX_MEMORY_STAGING = 1, NVX_MEMORY_FIRMWARE_IMAGE,
    NVX_MEMORY_COMMAND_STORAGE
} nvx_memory_kind_t;

typedef struct nvx_memory_handle {
    uint64_t token, gpu_virtual_address, size;
    uint32_t slot, generation;
} nvx_memory_handle_t;

typedef enum nvx_memory_slot_state {
    NVX_MEMORY_SLOT_FREE = 0, NVX_MEMORY_SLOT_RESERVED,
    NVX_MEMORY_SLOT_READY, NVX_MEMORY_SLOT_RELEASING
} nvx_memory_slot_state_t;

typedef struct nvx_memory_slot {
    nvx_dma_buffer_t dma;
    nvx_gpuva_allocation_t va;
    uint64_t token, requested_size;
    uint32_t generation;
    uint8_t kind, state;
} nvx_memory_slot_t;

typedef struct nvx_memory_info {
    void *cpu_address;
    uint64_t gpu_virtual_address;
    uint64_t size;
    nvx_memory_kind_t kind;
} nvx_memory_info_t;

/* Allocate this manager in static/long-lived storage; it is intentionally large. */
typedef struct nvx_memory_manager {
    spinlock_t lock;
    nvx_gpuva_space_t *va_space;
    nvx_memory_slot_t slots[NVX_MEMORY_MAX_OBJECTS];
    uint64_t next_token;
    uint32_t generation;
    unsigned address_bits;
    uint8_t initialized;
} nvx_memory_manager_t;

nvx_memory_status_t nvx_memory_init(nvx_memory_manager_t *, nvx_gpuva_space_t *,
                                    unsigned address_bits);
nvx_memory_status_t nvx_memory_alloc(nvx_memory_manager_t *, size_t,
                                     nvx_memory_kind_t, nvx_memory_handle_t *);
nvx_memory_status_t nvx_memory_get_info(nvx_memory_manager_t *,
                                         const nvx_memory_handle_t *,
                                         nvx_memory_info_t *);
nvx_memory_status_t nvx_memory_cpu_address(nvx_memory_manager_t *,
                                           const nvx_memory_handle_t *, void **);
nvx_memory_status_t nvx_memory_release(nvx_memory_manager_t *,
                                       nvx_memory_handle_t *);
nvx_memory_status_t nvx_memory_validate(nvx_memory_manager_t *);

#endif
