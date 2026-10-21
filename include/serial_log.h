/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_SERIAL_LOG_H
#define VANTA_SERIAL_LOG_H
#include <stddef.h>
#define SERIAL_LOG_CAPACITY 32768U
void serial_log_append(const char *data, size_t length);
size_t serial_log_snapshot(char *output, size_t capacity);
#endif
