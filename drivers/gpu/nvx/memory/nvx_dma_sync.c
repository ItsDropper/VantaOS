/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_dma_sync.h"
#include <string.h>
static int valid_buffer(const nvx_dma_sync_t *s)
{
    const nvx_dma_buffer_t *b;
    if(!s||!s->initialized||!s->buffer) return 0;
    b=s->buffer;
    return b->acquired&&b->cpu_address==(void *)s->cpu_address&&
           b->gpu_address==s->gpu_address&&b->size==s->size&&
           b->page_count==s->page_count&&b->allocation_id==s->allocation_id&&
           b->allocation_id!=0U&&b->page_count>0U;
}
static int valid_range(const nvx_dma_sync_t *s,size_t offset,size_t length)
{
    return length>0U&&offset<=s->size&&length<=s->size-offset;
}
static void device_write_barrier(void)
{
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __asm__ volatile("mfence" ::: "memory");
}
static void device_read_barrier(void)
{
    __asm__ volatile("mfence" ::: "memory");
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}
nvx_dma_sync_status_t nvx_dma_sync_init(nvx_dma_sync_t *s,nvx_dma_buffer_t *b)
{
    if(!s||!b) return NVX_DMA_SYNC_INVALID_ARGUMENT;
    if(s->initialized) return NVX_DMA_SYNC_INVALID_ARGUMENT;
    if(!b->acquired||!b->cpu_address||!b->size||!b->page_count||!b->gpu_address)
        return NVX_DMA_SYNC_BUFFER_INVALID;
    memset(s,0,sizeof(*s));
    s->buffer=b; s->cpu_address=(uintptr_t)b->cpu_address;
    s->gpu_address=b->gpu_address; s->size=b->size; s->page_count=b->page_count;
    s->allocation_id=b->allocation_id;
    s->owner=NVX_DMA_OWNER_CPU; s->initialized=1U;
    return NVX_DMA_SYNC_OK;
}
nvx_dma_sync_status_t nvx_dma_sync_for_device(nvx_dma_sync_t *s,
                                               size_t offset,size_t length)
{
    if(!s) return NVX_DMA_SYNC_INVALID_ARGUMENT;
    if(!s->initialized) return NVX_DMA_SYNC_NOT_INITIALIZED;
    if(!valid_buffer(s)) return NVX_DMA_SYNC_BUFFER_INVALID;
    if(!valid_range(s,offset,length)) return NVX_DMA_SYNC_INVALID_RANGE;
    if(s->owner!=NVX_DMA_OWNER_CPU) return NVX_DMA_SYNC_WRONG_OWNER;
    /* x86 coherent DMA still needs an explicit release/order point before handoff. */
    device_write_barrier(); s->owner=NVX_DMA_OWNER_DEVICE;
    return NVX_DMA_SYNC_OK;
}
nvx_dma_sync_status_t nvx_dma_sync_for_shared(nvx_dma_sync_t *s,
                                                size_t offset,size_t length)
{
    if(!s) return NVX_DMA_SYNC_INVALID_ARGUMENT;
    if(!s->initialized) return NVX_DMA_SYNC_NOT_INITIALIZED;
    if(!valid_buffer(s)) return NVX_DMA_SYNC_BUFFER_INVALID;
    if(!valid_range(s,offset,length)) return NVX_DMA_SYNC_INVALID_RANGE;
    if(s->owner!=NVX_DMA_OWNER_CPU) return NVX_DMA_SYNC_WRONG_OWNER;
    /*
     * NVX currently targets x86 coherent DMA. Shared queues are deliberately
     * not exclusive: the CPU produces commands/consumes replies while GSP
     * consumes commands/produces replies. Queue/MMIO code supplies ordering.
     */
    device_write_barrier();
    s->owner=NVX_DMA_OWNER_SHARED;
    return NVX_DMA_SYNC_OK;
}
nvx_dma_sync_status_t nvx_dma_sync_for_cpu(nvx_dma_sync_t *s,
                                            size_t offset,size_t length)
{
    if(!s) return NVX_DMA_SYNC_INVALID_ARGUMENT;
    if(!s->initialized) return NVX_DMA_SYNC_NOT_INITIALIZED;
    if(!valid_buffer(s)) return NVX_DMA_SYNC_BUFFER_INVALID;
    if(!valid_range(s,offset,length)) return NVX_DMA_SYNC_INVALID_RANGE;
    if(s->owner!=NVX_DMA_OWNER_DEVICE && s->owner!=NVX_DMA_OWNER_SHARED) return NVX_DMA_SYNC_WRONG_OWNER;
    device_read_barrier(); s->owner=NVX_DMA_OWNER_CPU;
    return NVX_DMA_SYNC_OK;
}
nvx_dma_sync_status_t nvx_dma_sync_destroy(nvx_dma_sync_t *s)
{
    if(!s) return NVX_DMA_SYNC_INVALID_ARGUMENT;
    if(!s->initialized) return NVX_DMA_SYNC_NOT_INITIALIZED;
    if(!valid_buffer(s)) return NVX_DMA_SYNC_BUFFER_INVALID;
    if(s->owner!=NVX_DMA_OWNER_CPU) return NVX_DMA_SYNC_WRONG_OWNER;
    memset(s,0,sizeof(*s)); return NVX_DMA_SYNC_OK;
}
