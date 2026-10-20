/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "dma.h"
#include "pmm.h"
#include <string.h>
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
 void *base_address = 0;
 uint64_t base_physical;
 if(!pages||!count||count>DMA_MAX_CONTIGUOUS_PAGES||count>UINT32_MAX)return 0;
 if(!pmm_alloc_contiguous_blocks((uint32_t)count,&base_address)||!base_address)return 0;
 base_physical=(uint64_t)(uintptr_t)base_address;
 for(size_t i=0U;i<count;i++){
  uint64_t physical=base_physical+(uint64_t)i*DMA_PAGE_SIZE;
  void *virtual_address=(void *)(uintptr_t)physical;
  if(physical>=DMA_LIMIT||physical>UINT64_MAX-DMA_PAGE_SIZE||
     !virtual_address||((uintptr_t)virtual_address&(DMA_PAGE_SIZE-1U))){
   for(size_t j=0U;j<count;j++)pmm_free_block((void *)(uintptr_t)(base_physical+(uint64_t)j*DMA_PAGE_SIZE));
   for(size_t j=0U;j<count;j++){pages[j].virtual_address=0;pages[j].physical_address=0;pages[j].size=0;}
   return 0;
  }
  pages[i].virtual_address=virtual_address;
  pages[i].physical_address=physical;
  pages[i].size=DMA_PAGE_SIZE;
 }
 for(size_t i=0U;i<count;i++){
  if(!dma_page_is_valid(&pages[i])){
   for(size_t j=0U;j<count;j++)pmm_free_block((void *)(uintptr_t)(base_physical+(uint64_t)j*DMA_PAGE_SIZE));
   for(size_t j=0U;j<count;j++){pages[j].virtual_address=0;pages[j].physical_address=0;pages[j].size=0;}
   return 0;
  }
  memset(pages[i].virtual_address,0,DMA_PAGE_SIZE);
 }
 return 1;
}
void dma_free_contiguous(dma_page_t* pages,size_t count) {
 if(!pages||count>DMA_MAX_CONTIGUOUS_PAGES)return;
 for(size_t i=0;i<count;i++)dma_free_page(&pages[i]);
}
