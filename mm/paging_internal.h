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
uint64_t* paging_user_leaf_entry(paging_address_space_t* space,
                                 uintptr_t virtual_address,
                                 int create);
void paging_release_user_page_tables(paging_address_space_t* space);

#endif
