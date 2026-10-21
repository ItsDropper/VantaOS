/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_PARTITION_SCAN_H
#define VANTA_PARTITION_SCAN_H
#include <stddef.h>
#include <stdint.h>
#define PARTITION_SCAN_MAX 32U
#define PARTITION_NAME_MAX 37U
typedef int (*partition_read_fn)(void *, uint64_t, uint32_t, void *);
typedef enum { PARTITION_SCHEME_MBR = 1, PARTITION_SCHEME_GPT = 2 } partition_scheme_t;
typedef struct {
    uint64_t start_lba;
    uint64_t sector_count;
    uint8_t mbr_type;
    uint8_t scheme;
    uint8_t type_guid[16];
    char name[PARTITION_NAME_MAX];
} partition_scan_entry_t;
typedef enum {
    PARTITION_SCAN_OK = 0, PARTITION_SCAN_INVALID_ARGUMENT,
    PARTITION_SCAN_IO_ERROR, PARTITION_SCAN_BAD_TABLE,
    PARTITION_SCAN_NO_PARTITIONS, PARTITION_SCAN_CAPACITY
} partition_scan_status_t;
partition_scan_status_t partition_scan(partition_read_fn read_sector,
    void *context, uint64_t disk_sector_count, partition_scan_entry_t *entries,
    size_t entry_capacity, size_t *out_count);
#endif
