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
 * The current x86 DMA backend is identity-mapped and coherent. Physical
 * memory is currently managed below 4 GiB; dma_mask expresses the device's
 * inclusive address limit so controllers with narrower masks are supported.
 * This interface deliberately does not claim IOMMU translation or streaming
 * cache-maintenance support.
 */
typedef struct
{
    uint64_t dma_mask;
} dma_device_t;

typedef struct
{
    void* virtual_address;
    uint64_t physical_address;
    size_t size;
} dma_page_t;

#define DMA_MAX_CONTIGUOUS_PAGES 16384U
#define DMA_DEFAULT_MASK 0xFFFFFFFFULL

int dma_alloc_page(dma_page_t* page);
int dma_alloc_page_for_device(const dma_device_t* device, dma_page_t* page);
int dma_alloc_contiguous(dma_page_t* pages, size_t count);
int dma_alloc_contiguous_for_device(const dma_device_t* device,
                                    dma_page_t* pages, size_t count);
void dma_free_contiguous(dma_page_t* pages, size_t count);
void dma_free_page(dma_page_t* page);
int dma_page_is_valid(const dma_page_t* page);
int dma_page_is_valid_for_device(const dma_device_t* device,
                                 const dma_page_t* page);

#endif
