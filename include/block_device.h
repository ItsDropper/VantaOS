/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef BLOCK_DEVICE_H
#define BLOCK_DEVICE_H
#include <stdint.h>
#include <stddef.h>
#define BLOCK_DEVICE_NAME_MAX 32U
#define BLOCK_DEVICE_MAX 16U
#define BLOCK_DEVICE_READ_ONLY (1U << 0)
#define BLOCK_DEVICE_REMOVABLE (1U << 1)
#define BLOCK_DEVICE_BOOT_MEDIA (1U << 2)
typedef enum {
 BLOCK_STATUS_OK=0, BLOCK_STATUS_INVALID, BLOCK_STATUS_NOT_READY,
 BLOCK_STATUS_READ_ONLY, BLOCK_STATUS_OUT_OF_RANGE, BLOCK_STATUS_UNSUPPORTED,
 BLOCK_STATUS_IO_ERROR, BLOCK_STATUS_TIMEOUT, BLOCK_STATUS_NO_MEMORY, BLOCK_STATUS_BUSY
} block_status_t;
typedef struct block_device block_device_t;
typedef struct {
 block_status_t (*read)(block_device_t*, uint64_t, uint32_t, void*);
 block_status_t (*write)(block_device_t*, uint64_t, uint32_t, const void*);
 block_status_t (*flush)(block_device_t*);
} block_device_ops_t;
/* Device storage and ops remain driver-owned and alive until unregister succeeds. */
struct block_device {
 char name[BLOCK_DEVICE_NAME_MAX];
 uint64_t sector_count;
 uint32_t sector_size;
 uint32_t flags;
 volatile uint32_t online;
 volatile uint32_t active_operations;
 const block_device_ops_t* ops;
 void* driver_data;
};
block_status_t block_device_register(block_device_t*);
block_status_t block_device_unregister(block_device_t*);
uint32_t block_device_count(void);
block_device_t* block_device_get(uint32_t);
block_device_t* block_device_find(const char*);
block_status_t block_device_read(block_device_t*, uint64_t, uint32_t, void*);
block_status_t block_device_write(block_device_t*, uint64_t, uint32_t, const void*);
block_status_t block_device_flush(block_device_t*);
#endif
