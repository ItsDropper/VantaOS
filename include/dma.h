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
 *
 * Polaris GCN4 requires a 32 MiB contiguous leaf table (4,194,304 64-bit
 * entries). Keep the contiguous allocator limit large enough for that table
 * plus normal ring and context allocations.
 */
typedef struct { void* virtual_address; uint64_t physical_address; size_t size; } dma_page_t;
#define DMA_MAX_CONTIGUOUS_PAGES 16384U
int dma_alloc_page(dma_page_t*);
int dma_alloc_contiguous(dma_page_t* pages, size_t count);
void dma_free_contiguous(dma_page_t* pages, size_t count);
void dma_free_page(dma_page_t*);
int dma_page_is_valid(const dma_page_t*);
#endif
