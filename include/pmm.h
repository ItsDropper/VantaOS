/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef PMM_H
#define PMM_H

#include "vanta_boot_info.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096U
#define PMM_MAX_PHYSICAL_ADDRESS 0x100000000ULL
#define PMM_MAX_BLOCKS 1048576U
#define PMM_BITMAP_WORDS (PMM_MAX_BLOCKS / 32U)

void pmm_initialize(const vanta_boot_info_t* boot_info);
void* pmm_alloc_block(void);
void pmm_free_block(void* ptr);
uint32_t pmm_get_total_blocks(void);
uint32_t pmm_get_used_blocks(void);
uint32_t pmm_get_free_blocks(void);
int pmm_is_initialized(void);

#endif
