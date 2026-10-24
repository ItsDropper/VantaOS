/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_DMA_PAGE_TABLE_H
#define VANTA_GPU_DMA_PAGE_TABLE_H

#include "gpu_dma.h"
#include "gpu_page_table.h"

/*
 * Allocate/free callbacks for gpu_page_table_ops_t and AMD Polaris VM runtime.
 * Allocations are physically contiguous, identity-mapped and DMA-visible.
 * Current x86 DMA support is limited to physical memory below 4 GiB.
 */
int gpu_dma_page_table_allocate(void *context, size_t entry_count,
                                gpu_page_table_page_t *out_page);
void gpu_dma_page_table_free(void *context, gpu_page_table_page_t *page);

#endif
