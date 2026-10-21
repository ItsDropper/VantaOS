/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "serial_log.h"
#include <stdint.h>
static char ring[SERIAL_LOG_CAPACITY];
static uint64_t write_position;

void serial_log_append(const char *data, size_t length)
{
    if (!data) return;
    for (size_t i = 0U; i < length; ++i) {
        uint64_t position = __atomic_fetch_add(&write_position, 1U, __ATOMIC_RELAXED);
        __atomic_store_n(&ring[position % SERIAL_LOG_CAPACITY], data[i], __ATOMIC_RELAXED);
    }
}

size_t serial_log_snapshot(char *output, size_t capacity)
{
    if (!output || !capacity) return 0U;
    uint64_t end = __atomic_load_n(&write_position, __ATOMIC_ACQUIRE);
    uint64_t available = end < SERIAL_LOG_CAPACITY ? end : SERIAL_LOG_CAPACITY;
    size_t count = available < capacity ? (size_t)available : capacity;
    uint64_t start = end - count;
    for (size_t i = 0U; i < count; ++i)
        output[i] = __atomic_load_n(&ring[(start + i) % SERIAL_LOG_CAPACITY], __ATOMIC_RELAXED);
    return count;
}
