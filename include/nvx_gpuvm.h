/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GPUVM_H
#define VANTA_NVX_GPUVM_H

#include <stddef.h>
#include <stdint.h>
#include "dma.h"
#include "nvx_gpuva.h"
#include "nvx_mmio.h"
#include "spinlock.h"

#define NVX_GPUVM_PAGE_SIZE       4096ULL
#define NVX_GPUVM_VA_BITS         49U
#define NVX_GPUVM_PA_BITS         47U
#define NVX_GPUVM_MAX_TABLES      256U
#define NVX_GPUVM_MAX_MAPPINGS    NVX_GPUVA_MAX_ALLOCS
#define NVX_GPUVM_ACCESS_READ     1U
#define NVX_GPUVM_ACCESS_WRITE    2U

typedef enum nvx_gpuvm_status {
    NVX_GPUVM_OK = 0,
    NVX_GPUVM_INVALID_ARGUMENT,
    NVX_GPUVM_INVALID_RANGE,
    NVX_GPUVM_NOT_INITIALIZED,
    NVX_GPUVM_BUSY,
    NVX_GPUVM_OUT_OF_VA,
    NVX_GPUVM_TABLES_EXHAUSTED,
    NVX_GPUVM_DMA_FAILED,
    NVX_GPUVM_PAGE_INVALID,
    NVX_GPUVM_ADDRESS_WIDTH,
    NVX_GPUVM_ALREADY_MAPPED,
    NVX_GPUVM_NOT_MAPPED,
    NVX_GPUVM_STALE_HANDLE,
    NVX_GPUVM_MMIO_ERROR,
    NVX_GPUVM_FLUSH_TIMEOUT,
    NVX_GPUVM_DEVICE_FAULTED,
    NVX_GPUVM_CORRUPT_STATE
} nvx_gpuvm_status_t;

typedef struct nvx_gpuvm_table {
    dma_page_t backing;
    uint16_t parent_index;
    uint16_t parent_slot;
    uint16_t entry_count;
    uint8_t level;
    uint8_t active;
} nvx_gpuvm_table_t;

typedef struct nvx_gpuvm_mapping {
    uint64_t token;
    uint64_t base;
    uint64_t size;
    uint32_t generation;
    uint8_t valid;
    uint8_t mapped;
} nvx_gpuvm_mapping_t;

typedef struct nvx_gpuvm_record {
    nvx_gpuva_allocation_t va;
    uint64_t token;
    uint64_t page_count;
    uint32_t generation;
    uint8_t active;
    uint8_t mapped;
} nvx_gpuvm_record_t;

/*
 * NVIDIA TU102/GA10x-style V2 page-table builder: 49-bit GPU VA, 47-bit
 * physical addresses, 4 KiB pages only, system-coherent PTEs. Large pages,
 * compression, VRAM aperture mappings and engine-specific PTE policy are not
 * implemented. Keep the manager in static/long-lived kernel storage.
 *
 * The MMIO mapping must be a validated BAR0 mapping covering 0xB830B4.
 * Page-table changes after activation require all GPU engines to be quiesced.
 * This is a bring-up primitive; it does not initialize the GSP or GR engines.
 */
typedef struct nvx_gpuvm {
    spinlock_t lock;
    nvx_gpuva_space_t va_space;
    nvx_gpuvm_table_t tables[NVX_GPUVM_MAX_TABLES];
    nvx_gpuvm_record_t mappings[NVX_GPUVM_MAX_MAPPINGS];
    nvx_mmio_region_t bar0;
    uint64_t next_token;
    uint32_t generation;
    uint16_t table_count;
    uint16_t mapping_count;
    uint16_t root_index;
    uint8_t initialized;
    uint8_t root_published;
    uint8_t quiesced;
    uint8_t faulted;
} nvx_gpuvm_t;

nvx_gpuvm_status_t nvx_gpuvm_init(nvx_gpuvm_t *vm, uint64_t va_start,
                                  uint64_t va_size,
                                  const nvx_mmio_region_t *bar0);
nvx_gpuvm_status_t nvx_gpuvm_reserve(nvx_gpuvm_t *vm, uint64_t size,
                                     uint64_t alignment, unsigned guard_pages,
                                     nvx_gpuvm_mapping_t *out_mapping);
nvx_gpuvm_status_t nvx_gpuvm_map_pages(nvx_gpuvm_t *vm,
                                       nvx_gpuvm_mapping_t *mapping,
                                       const uint64_t *physical_pages,
                                       size_t page_count, unsigned access);
nvx_gpuvm_status_t nvx_gpuvm_release(nvx_gpuvm_t *vm,
                                     nvx_gpuvm_mapping_t *mapping);
nvx_gpuvm_status_t nvx_gpuvm_activate(nvx_gpuvm_t *vm);
nvx_gpuvm_status_t nvx_gpuvm_flush(nvx_gpuvm_t *vm);
/* Caller asserts every engine that can use this PDB is stopped before setting true. */
nvx_gpuvm_status_t nvx_gpuvm_set_quiesced(nvx_gpuvm_t *vm, int quiesced);
nvx_gpuvm_status_t nvx_gpuvm_validate(nvx_gpuvm_t *vm);
nvx_gpuvm_status_t nvx_gpuvm_destroy(nvx_gpuvm_t *vm);

#endif
