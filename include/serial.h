/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>

void serial_initialize(void);
void serial_write(const char* text);
void serial_write_bytes(const char* data, size_t length);
/* Optional persistence checkpoint; a disabled or failed logger returns zero. */
int serial_boot_log_flush(void);

#endif
