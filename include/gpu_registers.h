/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_REGISTERS_H
#define VANTA_GPU_REGISTERS_H

#include <stddef.h>
#include <stdint.h>
#include "gpu_mmio.h"

typedef enum gpu_register_status {
    GPU_REGISTER_OK = 0,
    GPU_REGISTER_INVALID,
    GPU_REGISTER_IO_ERROR,
    GPU_REGISTER_TIMEOUT,
    GPU_REGISTER_VERIFY_FAILED
} gpu_register_status_t;

typedef struct gpu_register_field {
    uint32_t mask;
    uint8_t shift;
} gpu_register_field_t;

typedef struct gpu_register_write {
    size_t offset;
    uint32_t value;
    uint32_t verify_mask;
} gpu_register_write_t;

gpu_register_status_t gpu_register_read(const gpu_mmio_region_t *region,
                                        size_t offset, uint32_t *value);
gpu_register_status_t gpu_register_write(const gpu_mmio_region_t *region,
                                         size_t offset, uint32_t value);
gpu_register_status_t gpu_register_update(const gpu_mmio_region_t *region,
                                          size_t offset, uint32_t clear_mask,
                                          uint32_t set_mask,
                                          uint32_t *out_value);
gpu_register_status_t gpu_register_poll(const gpu_mmio_region_t *region,
                                        size_t offset, uint32_t mask,
                                        uint32_t expected,
                                        uint32_t max_reads,
                                        uint32_t *last_value);
gpu_register_status_t gpu_register_field_encode(gpu_register_field_t field,
                                                uint32_t value,
                                                uint32_t *encoded);
uint32_t gpu_register_field_decode(gpu_register_field_t field,
                                   uint32_t register_value);
/* Applies ordered writes and verifies selected bits after each write. */
gpu_register_status_t gpu_register_apply_sequence(
    const gpu_mmio_region_t *region, const gpu_register_write_t *writes,
    size_t count, uint32_t max_reads_per_verify, size_t *failed_index);

#endif
