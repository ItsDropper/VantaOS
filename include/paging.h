#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

void paging_initialize(void);
int paging_map_page(uintptr_t virtual_address, uintptr_t physical_address);
void paging_unmap_page(uintptr_t virtual_address);
uintptr_t paging_get_physical(uintptr_t virtual_address);

#endif
