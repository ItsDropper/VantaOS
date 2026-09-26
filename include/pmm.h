#ifndef PMM_H
#define PMM_H

#include "multiboot.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096

void pmm_initialize(multiboot_info_t* mbd);

void* pmm_alloc_block(void);

void pmm_free_block(void* ptr);

#endif