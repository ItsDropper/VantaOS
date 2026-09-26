#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

#define HEAP_ALIGNMENT 8
#define HEAP_VIRTUAL_BASE 0x40000000U
#define HEAP_MAX_SIZE (4 * 1024 * 1024)

void heap_initialize(void);
void* kmalloc(size_t size);
void kfree(void* ptr);

size_t heap_get_total_size(void);
size_t heap_get_used_size(void);
size_t heap_get_free_size(void);
int heap_is_initialized(void);

#endif
