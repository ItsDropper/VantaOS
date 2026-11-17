/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef PAGING_INTERNAL_H
#define PAGING_INTERNAL_H

#include <stdint.h>
#include "paging.h"

/* Private contract shared by page-table core and user-mapping implementation. */
#define PAGING_TABLE_ENTRIES 512U
#define PAGE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL

uint64_t paging_lock_acquire(void);
void paging_lock_release(uint64_t saved_flags);
_Noreturn void paging_tlb_failure(void);
int paging_owns_address_space(const paging_address_space_t* space);
/* Pin an address space against teardown and serialize only its page-table tree. */
int paging_space_lock_acquire(paging_address_space_t* space,
                              uint64_t* saved_flags);
void paging_space_lock_release(paging_address_space_t* space,
                               uint64_t saved_flags);
uint64_t* paging_user_leaf_entry(paging_address_space_t* space,
                                 uintptr_t virtual_address,
                                 int create);
/* Unlink PML4 slot 1 under the paging lock; returns the old entry. */
uint64_t paging_detach_user_page_tables(paging_address_space_t* space);
/*
 * Free a detached user table tree, private demand frames, and shared-cache
 * mapping references. The tree is unreachable from any address space.
 */
uint32_t paging_free_detached_user_tables(uint64_t pml4_entry);

#endif
