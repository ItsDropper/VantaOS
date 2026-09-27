#ifndef PMM_H
#define PMM_H

#include "multiboot.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096U
#define PMM_MAX_PHYSICAL_ADDRESS 0x100000000ULL
#define PMM_MAX_BLOCKS 1048576U
#define PMM_BITMAP_WORDS (PMM_MAX_BLOCKS / 32U)

void pmm_initialize(multiboot_info_t* mbd);
void* pmm_alloc_block(void);
void pmm_free_block(void* ptr);
uint32_t pmm_get_total_blocks(void);
uint32_t pmm_get_used_blocks(void);
uint32_t pmm_get_free_blocks(void);
int pmm_is_initialized(void);

#endif
