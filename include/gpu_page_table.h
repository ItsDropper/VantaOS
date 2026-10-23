/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_PAGE_TABLE_H
#define VANTA_GPU_PAGE_TABLE_H

#include <stddef.h>
#include <stdint.h>

#define GPU_PAGE_TABLE_LEVELS 4U
#define GPU_PAGE_TABLE_ENTRIES 512U
#define GPU_PAGE_TABLE_MAX_PAGES 512U
#define GPU_PAGE_TABLE_PAGE_SIZE 4096U
#define GPU_PAGE_TABLE_ADDRESS_MASK 0x0000FFFFFFFFF000ULL

typedef struct gpu_page_table_page {
    uint64_t *entries;
    uint64_t physical_address;
    void *cookie;
} gpu_page_table_page_t;

typedef enum gpu_page_table_status {
    GPU_PAGE_TABLE_OK = 0,
    GPU_PAGE_TABLE_INVALID_ARGUMENT,
    GPU_PAGE_TABLE_NOT_INITIALIZED,
    GPU_PAGE_TABLE_ALLOCATION_FAILED,
    GPU_PAGE_TABLE_ADDRESS_INVALID,
    GPU_PAGE_TABLE_ALREADY_MAPPED,
    GPU_PAGE_TABLE_NOT_MAPPED,
    GPU_PAGE_TABLE_TABLE_FULL,
    GPU_PAGE_TABLE_ENCODE_FAILED,
    GPU_PAGE_TABLE_FLUSH_FAILED,
    GPU_PAGE_TABLE_FAULTED,
    GPU_PAGE_TABLE_CORRUPT
} gpu_page_table_status_t;

typedef struct gpu_page_table_ops {
    void *context;
    int (*allocate_page)(void *context, gpu_page_table_page_t *out_page);
    void (*free_page)(void *context, gpu_page_table_page_t *page);
    int (*encode_directory)(void *context, uint64_t child_physical,
                            uint64_t *out_entry);
    int (*encode_leaf)(void *context, uint64_t physical, unsigned access,
                       uint64_t *out_entry);
    int (*flush)(void *context, uint64_t root_physical);
} gpu_page_table_ops_t;

typedef struct gpu_page_table {
    gpu_page_table_ops_t ops;
    gpu_page_table_page_t pages[GPU_PAGE_TABLE_MAX_PAGES];
    size_t page_count;
    uint64_t root_physical;
    uint8_t initialized;
    uint8_t faulted;
} gpu_page_table_t;

/*
 * Software 4-level, 48-bit GPU page-table walker. Vendor callbacks own PTE/PDE
 * encoding, DMA-coherent page allocation, hardware root installation and TLB
 * invalidation. Caller must quiesce the GPU before destroy frees any table.
 */
gpu_page_table_status_t gpu_page_table_init(
    gpu_page_table_t *table, const gpu_page_table_ops_t *ops);
gpu_page_table_status_t gpu_page_table_map(
    gpu_page_table_t *table, uint64_t gpu_address, uint64_t physical_address,
    unsigned access);
gpu_page_table_status_t gpu_page_table_unmap(
    gpu_page_table_t *table, uint64_t gpu_address);
gpu_page_table_status_t gpu_page_table_flush(gpu_page_table_t *table);
gpu_page_table_status_t gpu_page_table_destroy(
    gpu_page_table_t *table, int device_quiesced);

#endif
