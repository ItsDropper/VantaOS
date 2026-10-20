/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_ring.h"
#include <string.h>
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U); }
static void put32(uint8_t *p,uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8U); p[2]=(uint8_t)(v>>16U); p[3]=(uint8_t)(v>>24U); }
static uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p)|((uint64_t)get32(p+4U)<<32U); }
static int overlaps(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!an||!bn) return 0;
    if(x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn) return 1;
    return x<y+bn&&y<x+an;
}
static uint32_t load_ptr(const volatile uint32_t *p) { return __atomic_load_n(p,__ATOMIC_ACQUIRE); }
static void store_ptr(volatile uint32_t *p,uint32_t v) { __atomic_store_n(p,v,__ATOMIC_RELEASE); }
static void ring_copy_in(nvx_gsp_ring_t *r,uint32_t page,const uint8_t *src,size_t bytes) {
    size_t total=(size_t)r->capacity_pages*NVX_GSP_PAGE_SIZE;
    size_t offset=(size_t)page*NVX_GSP_PAGE_SIZE;
    size_t first=total-offset; if(first>bytes) first=bytes;
    memcpy(r->storage+offset,src,first);
    if(first<bytes) memcpy(r->storage,src+first,bytes-first);
}
static void ring_copy_out(nvx_gsp_ring_t *r,uint32_t page,uint8_t *dst,size_t bytes) {
    size_t total=(size_t)r->capacity_pages*NVX_GSP_PAGE_SIZE;
    size_t offset=(size_t)page*NVX_GSP_PAGE_SIZE;
    size_t first=total-offset; if(first>bytes) first=bytes;
    memcpy(dst,r->storage+offset,first);
    if(first<bytes) memcpy(dst+first,r->storage,bytes-first);
}
static uint32_t checksum_fold(const uint8_t *data,size_t bytes) {
    uint64_t x=0U;
    for(size_t i=0;i<bytes;i+=8U) x^=get64(data+i);
    return (uint32_t)x^(uint32_t)(x>>32U);
}
nvx_gsp_ring_status_t nvx_gsp_ring_init(nvx_gsp_ring_t *r,void *storage,
    size_t pages,volatile uint32_t *read_ptr,volatile uint32_t *write_ptr,
    nvx_gsp_ring_notify_t notify,void *context)
{
    if(!r||!storage||!read_ptr||!write_ptr) return NVX_GSP_RING_INVALID_ARGUMENT;
    if(r->initialized) return NVX_GSP_RING_INVALID_ARGUMENT;
    if(pages<2U||pages>NVX_GSP_MAX_QUEUE_PAGES||
       ((uintptr_t)storage&(NVX_GSP_PAGE_SIZE-1U))!=0U)
        return NVX_GSP_RING_INVALID_ARGUMENT;
    if(load_ptr(read_ptr)>=pages||load_ptr(write_ptr)>=pages)
        return NVX_GSP_RING_BAD_POINTER;
    memset(r,0,sizeof(*r)); spinlock_init(&r->lock);
    r->storage=storage; r->capacity_pages=(uint32_t)pages;
    r->read_ptr=read_ptr; r->write_ptr=write_ptr; r->notify=notify;
    r->notify_context=context; r->next_sequence=1U; r->initialized=1U;
    return NVX_GSP_RING_OK;
}
nvx_gsp_ring_status_t nvx_gsp_ring_send(nvx_gsp_ring_t *r,
    uint32_t header_version,uint32_t signature,uint32_t function,
    const void *payload,size_t payload_size,void *scratch,size_t scratch_size,
    uint32_t *out_sequence)
{
    size_t rpc_len,total_len,pages;
    uint8_t *msg=(uint8_t *)scratch;
    uint32_t read,write,used,free_pages,sequence;
    nvx_gsp_ring_notify_t notify; void *notify_context;
    if(out_sequence) *out_sequence=0U;
    if(!r||!scratch||!out_sequence||(payload_size&&!payload)) return NVX_GSP_RING_INVALID_ARGUMENT;
    if(!r->initialized) return NVX_GSP_RING_NOT_INITIALIZED;
    if(payload_size>SIZE_MAX-NVX_GSP_RPC_HEADER_SIZE) return NVX_GSP_RING_MESSAGE_TOO_LARGE;
    rpc_len=NVX_GSP_RPC_HEADER_SIZE+payload_size;
    if(rpc_len>NVX_GSP_MAX_MESSAGE_PAGES*(size_t)NVX_GSP_PAGE_SIZE-NVX_GSP_MSG_HEADER_SIZE)
        return NVX_GSP_RING_MESSAGE_TOO_LARGE;
    if(rpc_len>SIZE_MAX-NVX_GSP_MSG_HEADER_SIZE-(NVX_GSP_PAGE_SIZE-1U))
        return NVX_GSP_RING_MESSAGE_TOO_LARGE;
    total_len=((NVX_GSP_MSG_HEADER_SIZE+rpc_len+NVX_GSP_PAGE_SIZE-1U)/NVX_GSP_PAGE_SIZE)*NVX_GSP_PAGE_SIZE;
    pages=total_len/NVX_GSP_PAGE_SIZE;
    if(scratch_size<total_len) return NVX_GSP_RING_BUFFER_TOO_SMALL;
    if(overlaps(scratch,total_len,r->storage,(size_t)r->capacity_pages*NVX_GSP_PAGE_SIZE)||
       overlaps(scratch,total_len,payload,payload_size)) return NVX_GSP_RING_INVALID_ARGUMENT;
    memset(msg,0,total_len);
    put32(msg+NVX_GSP_MSG_HEADER_SIZE+0U,header_version);
    put32(msg+NVX_GSP_MSG_HEADER_SIZE+4U,signature);
    put32(msg+NVX_GSP_MSG_HEADER_SIZE+8U,(uint32_t)rpc_len);
    put32(msg+NVX_GSP_MSG_HEADER_SIZE+12U,function);
    if(payload_size) memcpy(msg+NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE,payload,payload_size);
    spinlock_lock(&r->lock);
    read=load_ptr(r->read_ptr); write=load_ptr(r->write_ptr);
    if(read>=r->capacity_pages||write>=r->capacity_pages) {
        spinlock_unlock(&r->lock); return NVX_GSP_RING_BAD_POINTER;
    }
    used=(write+r->capacity_pages-read)%r->capacity_pages;
    free_pages=r->capacity_pages-used-1U;
    if(pages>free_pages) { spinlock_unlock(&r->lock); return NVX_GSP_RING_QUEUE_FULL; }
    sequence=r->next_sequence++;
    if(r->next_sequence==0U) r->next_sequence=1U;
    put32(msg+36U,sequence); put32(msg+40U,(uint32_t)pages);
    put32(msg+NVX_GSP_MSG_HEADER_SIZE+24U,sequence);
    put32(msg+32U,checksum_fold(msg,total_len));
    ring_copy_in(r,write,msg,total_len);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    store_ptr(r->write_ptr,(write+(uint32_t)pages)%r->capacity_pages);
    notify=r->notify; notify_context=r->notify_context;
    spinlock_unlock(&r->lock);
    *out_sequence=sequence;
    if(notify) notify(notify_context);
    return NVX_GSP_RING_OK;
}
nvx_gsp_ring_status_t nvx_gsp_ring_receive(nvx_gsp_ring_t *r,void *scratch,
    size_t scratch_size,nvx_gsp_rpc_view_t *out)
{
    uint8_t header[NVX_GSP_MSG_HEADER_SIZE]; uint8_t *msg=(uint8_t *)scratch;
    uint32_t read,write,pages,rpc_len,expected_pages;
    size_t bytes; nvx_gsp_ring_status_t status=NVX_GSP_RING_OK;
    if(out) memset(out,0,sizeof(*out));
    if(!r||!scratch||!out) return NVX_GSP_RING_INVALID_ARGUMENT;
    if(!r->initialized) return NVX_GSP_RING_NOT_INITIALIZED;
    if(overlaps(scratch,scratch_size,r->storage,(size_t)r->capacity_pages*NVX_GSP_PAGE_SIZE))
        return NVX_GSP_RING_INVALID_ARGUMENT;
    spinlock_lock(&r->lock);
    read=load_ptr(r->read_ptr); write=load_ptr(r->write_ptr);
    if(read>=r->capacity_pages||write>=r->capacity_pages) status=NVX_GSP_RING_BAD_POINTER;
    else if(read==write) status=NVX_GSP_RING_QUEUE_EMPTY;
    if(status!=NVX_GSP_RING_OK) { spinlock_unlock(&r->lock); return status; }
    ring_copy_out(r,read,header,sizeof(header));
    pages=get32(header+40U);
    if(!pages||pages>NVX_GSP_MAX_MESSAGE_PAGES||pages>=r->capacity_pages) {
        spinlock_unlock(&r->lock); return NVX_GSP_RING_BAD_MESSAGE;
    }
    if((write+r->capacity_pages-read)%r->capacity_pages<pages) {
        spinlock_unlock(&r->lock); return NVX_GSP_RING_BAD_MESSAGE;
    }
    bytes=(size_t)pages*NVX_GSP_PAGE_SIZE;
    if(scratch_size<bytes) { spinlock_unlock(&r->lock); return NVX_GSP_RING_BUFFER_TOO_SMALL; }
    ring_copy_out(r,read,msg,bytes);
    if(checksum_fold(msg,bytes)!=0U) { spinlock_unlock(&r->lock); return NVX_GSP_RING_BAD_CHECKSUM; }
    rpc_len=get32(msg+NVX_GSP_MSG_HEADER_SIZE+8U);
    if(rpc_len<NVX_GSP_RPC_HEADER_SIZE||rpc_len>bytes-NVX_GSP_MSG_HEADER_SIZE) {
        spinlock_unlock(&r->lock); return NVX_GSP_RING_BAD_MESSAGE;
    }
    expected_pages=(uint32_t)((NVX_GSP_MSG_HEADER_SIZE+(size_t)rpc_len+NVX_GSP_PAGE_SIZE-1U)/NVX_GSP_PAGE_SIZE);
    if(expected_pages!=pages) { spinlock_unlock(&r->lock); return NVX_GSP_RING_BAD_MESSAGE; }
    out->header_version=get32(msg+48U); out->signature=get32(msg+52U);
    out->length=rpc_len; out->function=get32(msg+60U);
    out->result=get32(msg+64U); out->result_private=get32(msg+68U);
    out->sequence=get32(msg+72U); out->spare=get32(msg+76U);
    out->payload=msg+NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE;
    out->payload_size=rpc_len-NVX_GSP_RPC_HEADER_SIZE;
    out->message_sequence=get32(msg+36U); out->element_pages=pages;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    store_ptr(r->read_ptr,(read+pages)%r->capacity_pages);
    spinlock_unlock(&r->lock);
    return NVX_GSP_RING_OK;
}
