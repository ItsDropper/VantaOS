/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "dma.h"
#include "pmm.h"
#define DMA_PAGE_SIZE 4096U
#define DMA_LIMIT 0x100000000ULL
int dma_alloc_page(dma_page_t* p) {
 if(!p||!pmm_is_initialized())return 0;
 p->virtual_address=0;p->physical_address=0;p->size=0;
 void* m=pmm_alloc_block();if(!m)return 0;
 uint64_t phys=(uint64_t)(uintptr_t)m;
 if((phys&(DMA_PAGE_SIZE-1U))||phys>=DMA_LIMIT){pmm_free_block(m);return 0;}
 unsigned char* b=(unsigned char*)m;
 for(uint32_t i=0;i<DMA_PAGE_SIZE;i++)b[i]=0;
 p->virtual_address=m;p->physical_address=phys;p->size=DMA_PAGE_SIZE;return 1;
}
int dma_page_is_valid(const dma_page_t* p) {
 return p&&p->virtual_address&&p->size==DMA_PAGE_SIZE&&p->physical_address<DMA_LIMIT&&
 !(p->physical_address&(DMA_PAGE_SIZE-1U))&&
 !((uintptr_t)p->virtual_address&(DMA_PAGE_SIZE-1U))&&
 (uint64_t)(uintptr_t)p->virtual_address==p->physical_address;
}
void dma_free_page(dma_page_t* p) {
 if(!dma_page_is_valid(p))return;
 pmm_free_block(p->virtual_address);p->virtual_address=0;p->physical_address=0;p->size=0;
}

int dma_alloc_contiguous(dma_page_t* pages, size_t count) {
 if(!pages||!count||count>64U)return 0;
 for(size_t i=0;i<count;i++){
  if(!dma_alloc_page(&pages[i])){
   while(i>0U)dma_free_page(&pages[--i]);
   return 0;
  }
  if(i>0U&&pages[i].physical_address!=pages[i-1U].physical_address+PAGE_SIZE){
   for(size_t j=0;j<=i;j++)dma_free_page(&pages[j]);
   return 0;
  }
 }
 return 1;
}
void dma_free_contiguous(dma_page_t* pages,size_t count) {
 if(!pages||count>64U)return;
 for(size_t i=0;i<count;i++)dma_free_page(&pages[i]);
}
