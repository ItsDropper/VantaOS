#include "mmio.h"
#include "paging.h"
#include "spinlock.h"
#include <stdint.h>

#define MMIO_PAGE_SIZE 4096ULL
#define MMIO_ALLOCATION_MAX 16U
#define MMIO_PHYSICAL_LIMIT 0x0010000000000000ULL

typedef struct {
    void* returned_address;
    uintptr_t virtual_base;
    uint64_t physical_base;
    size_t length;
    uint32_t page_count;
    uint32_t used;
} mmio_allocation_t;

static mmio_allocation_t allocations[MMIO_ALLOCATION_MAX];
static spinlock_t mmio_lock=SPINLOCK_INITIALIZER;

void* mmio_map_region(uint64_t physical_address,size_t length)
{
    if(!length || physical_address>=MMIO_PHYSICAL_LIMIT ||
       physical_address>UINT64_MAX-(uint64_t)(length-1U) ||
       (uint64_t)(length-1U)>=(MMIO_PHYSICAL_LIMIT-physical_address))
        return 0;
    uint64_t offset=physical_address&(MMIO_PAGE_SIZE-1U);
    if((uint64_t)length>UINT64_MAX-offset)return 0;
    uint64_t span=offset+(uint64_t)length;
    uint64_t page_count64=(span+MMIO_PAGE_SIZE-1U)/MMIO_PAGE_SIZE;
    uint64_t window_pages=PAGING_MMIO_SIZE/MMIO_PAGE_SIZE;
    if(!page_count64 || page_count64>window_pages)return 0;
    uint32_t page_count=(uint32_t)page_count64;
    uint64_t physical_base=physical_address&~(MMIO_PAGE_SIZE-1U);
    uint64_t flags=spinlock_lock_irqsave(&mmio_lock);
    int slot=-1;
    for(uint32_t i=0;i<MMIO_ALLOCATION_MAX;i++)
        if(!allocations[i].used){slot=(int)i;break;}
    if(slot<0){spinlock_unlock_irqrestore(&mmio_lock,flags);return 0;}

    uintptr_t chosen=0;
    uint64_t window_start=PAGING_MMIO_BASE;
    uint64_t window_end=PAGING_MMIO_BASE+PAGING_MMIO_SIZE;
    for(uint64_t candidate=window_start;
        candidate+(uint64_t)page_count*MMIO_PAGE_SIZE<=window_end;
        candidate+=MMIO_PAGE_SIZE) {
        uint32_t free_pages=0;
        for(;free_pages<page_count;free_pages++) {
            uintptr_t va=(uintptr_t)(candidate+(uint64_t)free_pages*MMIO_PAGE_SIZE);
            if(paging_get_page_flags(va)&PAGE_PRESENT)break;
        }
        if(free_pages==page_count){chosen=(uintptr_t)candidate;break;}
        candidate+=(uint64_t)free_pages*MMIO_PAGE_SIZE;
    }
    if(!chosen){spinlock_unlock_irqrestore(&mmio_lock,flags);return 0;}

    uint32_t mapped=0;
    for(;mapped<page_count;mapped++) {
        uintptr_t va=chosen+(uintptr_t)mapped*(uintptr_t)MMIO_PAGE_SIZE;
        uint64_t pa=physical_base+(uint64_t)mapped*MMIO_PAGE_SIZE;
        if(!paging_map_mmio_page(va,(uintptr_t)pa))break;
    }
    if(mapped!=page_count) {
        while(mapped>0) {
            --mapped;
            paging_unmap_page(chosen+(uintptr_t)mapped*(uintptr_t)MMIO_PAGE_SIZE);
        }
        spinlock_unlock_irqrestore(&mmio_lock,flags);return 0;
    }

    void* returned=(void*)(chosen+(uintptr_t)offset);
    allocations[slot].returned_address=returned;
    allocations[slot].virtual_base=chosen;
    allocations[slot].physical_base=physical_base;
    allocations[slot].length=length;
    allocations[slot].page_count=page_count;
    allocations[slot].used=1;
    spinlock_unlock_irqrestore(&mmio_lock,flags);
    return returned;
}

int mmio_unmap_region(void* address,size_t length)
{
    if(!address||!length)return 0;
    uint64_t flags=spinlock_lock_irqsave(&mmio_lock);
    for(uint32_t i=0;i<MMIO_ALLOCATION_MAX;i++) {
        mmio_allocation_t* a=&allocations[i];
        if(!a->used||a->returned_address!=address||a->length!=length)continue;
        for(uint32_t p=0;p<a->page_count;p++)
            paging_unmap_page(a->virtual_base+(uintptr_t)p*(uintptr_t)MMIO_PAGE_SIZE);
        a->returned_address=0;a->virtual_base=0;a->physical_base=0;
        a->length=0;a->page_count=0;a->used=0;
        spinlock_unlock_irqrestore(&mmio_lock,flags);
        return 1;
    }
    spinlock_unlock_irqrestore(&mmio_lock,flags);
    return 0;
}
