/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

#define HEAP_ALIGNMENT 8U
#define HEAP_VIRTUAL_BASE 0xC0000000U
#define HEAP_MAX_SIZE (64U * 1024U * 1024U)

void heap_initialize(void);
void* kmalloc(size_t size);
void kfree(void* ptr);

size_t heap_get_total_size(void);
size_t heap_get_used_size(void);
size_t heap_get_free_size(void);
int heap_is_initialized(void);

#endif
