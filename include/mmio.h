#ifndef MMIO_H
#define MMIO_H
#include <stddef.h>
#include <stdint.h>
/*
 * Reserve a contiguous virtual mapping for a physical MMIO range. The returned
 * pointer includes the physical address's page offset. Unmap with the exact
 * pointer and length. The allocator never reuses occupied kernel page-table
 * entries and refuses requests outside the fixed kernel MMIO virtual window.
 */
void* mmio_map_region(uint64_t physical_address,size_t length);
int mmio_unmap_region(void* virtual_address,size_t length);
#endif
