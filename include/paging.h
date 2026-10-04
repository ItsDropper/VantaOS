#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_SIZE 4096U
#define PAGE_PRESENT 0x001ULL
#define PAGE_WRITE   0x002ULL
#define PAGE_USER    0x004ULL
#define PAGE_LARGE   0x080ULL

typedef struct paging_address_space
{
    uint64_t* directory;
    uintptr_t directory_physical;
    int used;
} paging_address_space_t;

void paging_initialize(void);

int paging_map_page(uintptr_t virtual_address,
                    uintptr_t physical_address);

void paging_unmap_page(uintptr_t virtual_address);

uintptr_t paging_get_physical(uintptr_t virtual_address);

paging_address_space_t* paging_create_address_space(void);
void paging_destroy_address_space(paging_address_space_t* address_space);
void paging_switch_address_space(
    paging_address_space_t* address_space
);
paging_address_space_t* paging_get_kernel_address_space(void);
paging_address_space_t* paging_get_current_address_space(void);

#endif
