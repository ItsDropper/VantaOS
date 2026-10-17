#ifndef DMA_H
#define DMA_H
#include <stdint.h>
#include <stddef.h>
/*
 * Current x86 contract: PMM pages below 4 GiB are identity mapped and the
 * platform is DMA cache-coherent. Allocations are one page each, not a
 * promise that multiple calls return physically adjacent pages.
 */
typedef struct { void* virtual_address; uint64_t physical_address; size_t size; } dma_page_t;
int dma_alloc_page(dma_page_t*);
int dma_alloc_contiguous(dma_page_t* pages, size_t count);
void dma_free_contiguous(dma_page_t* pages, size_t count);
void dma_free_page(dma_page_t*);
int dma_page_is_valid(const dma_page_t*);
#endif
