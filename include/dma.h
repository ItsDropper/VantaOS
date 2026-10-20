/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

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
#define DMA_MAX_CONTIGUOUS_PAGES 4096U
int dma_alloc_page(dma_page_t*);
int dma_alloc_contiguous(dma_page_t* pages, size_t count);
void dma_free_contiguous(dma_page_t* pages, size_t count);
void dma_free_page(dma_page_t*);
int dma_page_is_valid(const dma_page_t*);
#endif
