/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_dma_sync.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
static uint8_t memory[4096] __attribute__((aligned(4096)));
int main(void) {
 nvx_dma_buffer_t b={0}; nvx_dma_sync_t s={0};
 b.cpu_address=memory; b.gpu_address=0x100000U; b.size=3000U;
 b.allocated_size=4096U; b.page_count=1U; b.allocation_id=7U; b.acquired=1U;
 assert(nvx_dma_sync_init(0,&b)==NVX_DMA_SYNC_INVALID_ARGUMENT);
 assert(nvx_dma_sync_init(&s,0)==NVX_DMA_SYNC_INVALID_ARGUMENT);
 assert(nvx_dma_sync_init(&s,&b)==NVX_DMA_SYNC_OK);
 assert(s.owner==NVX_DMA_OWNER_CPU);
 assert(nvx_dma_sync_for_device(&s,0U,0U)==NVX_DMA_SYNC_INVALID_RANGE);
 assert(nvx_dma_sync_for_device(&s,3000U,1U)==NVX_DMA_SYNC_INVALID_RANGE);
 assert(nvx_dma_sync_for_device(&s,SIZE_MAX,2U)==NVX_DMA_SYNC_INVALID_RANGE);
 assert(nvx_dma_sync_for_device(&s,16U,512U)==NVX_DMA_SYNC_OK);
 assert(s.owner==NVX_DMA_OWNER_DEVICE);
 assert(nvx_dma_sync_for_device(&s,0U,4U)==NVX_DMA_SYNC_WRONG_OWNER);
 assert(nvx_dma_sync_destroy(&s)==NVX_DMA_SYNC_WRONG_OWNER);
 b.acquired=0U;
 assert(nvx_dma_sync_for_cpu(&s,0U,1U)==NVX_DMA_SYNC_BUFFER_INVALID);
 b.acquired=1U;
 assert(nvx_dma_sync_for_cpu(&s,0U,3000U)==NVX_DMA_SYNC_OK);
 assert(s.owner==NVX_DMA_OWNER_CPU);
 assert(nvx_dma_sync_for_cpu(&s,0U,1U)==NVX_DMA_SYNC_WRONG_OWNER);
 assert(nvx_dma_sync_destroy(&s)==NVX_DMA_SYNC_OK);
 assert(!s.initialized&&s.buffer==0);
 assert(nvx_dma_sync_destroy(&s)==NVX_DMA_SYNC_NOT_INITIALIZED);
 assert(nvx_dma_sync_for_device(&s,0U,1U)==NVX_DMA_SYNC_NOT_INITIALIZED);
 b.acquired=0U;
 assert(nvx_dma_sync_init(&s,&b)==NVX_DMA_SYNC_BUFFER_INVALID);
 return 0;
}
