/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_ring.h"
#include <string.h>
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U);
}
static void put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8U); p[2]=(uint8_t)(v>>16U); p[3]=(uint8_t)(v>>24U);
}
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
nvx_gsp_ring_status_t nvx_gsp_ring_init(nvx_gsp_ring_t *r,void *storage,
    size_t pages,volatile uint32_t *read_ptr,volatile uint32_t *write_ptr,
    nvx_gsp_ring_notify_t notify,void *context)
{
    if(!r||!storage||!read_ptr||!write_ptr) return NVX_GSP_RING_INVALID_ARGUMENT;
    if(r->initialized) return NVX_GSP_RING_INVALID_ARGUMENT;
    if(pages<2U||pages>NVX_GSP_MAX_QUEUE_PAGES||
       ((uintptr_t)storage&(NVX_GSP_PAGE_SIZE-1U))!=0U)
        return NVX_GSP_RING_INVALID_ARGUMENT;
    size_t storage_bytes=pages*(size_t)NVX_GSP_PAGE_SIZE;
    if(overlaps(r,sizeof(*r),storage,storage_bytes)||
       overlaps(r,sizeof(*r),(const void *)read_ptr,sizeof(*read_ptr))||
       overlaps(r,sizeof(*r),(const void *)write_ptr,sizeof(*write_ptr))||
       overlaps(storage,storage_bytes,(const void *)read_ptr,sizeof(*read_ptr))||
       overlaps(storage,storage_bytes,(const void *)write_ptr,sizeof(*write_ptr))||
       overlaps((const void *)read_ptr,sizeof(*read_ptr),
                (const void *)write_ptr,sizeof(*write_ptr)))
        return NVX_GSP_RING_INVALID_ARGUMENT;
    if((uint32_t)(load_ptr(write_ptr)-load_ptr(read_ptr))>pages)
        return NVX_GSP_RING_BAD_POINTER;
    memset(r,0,sizeof(*r)); spinlock_init(&r->lock);
    r->storage=storage; r->capacity_pages=(uint32_t)pages;
    r->read_ptr=read_ptr; r->write_ptr=write_ptr; r->notify=notify;
    r->notify_context=context; r->next_sequence=1U; r->initialized=1U;
    return NVX_GSP_RING_OK;
}
nvx_gsp_ring_status_t nvx_gsp_ring_send(nvx_gsp_ring_t *r,
    uint32_t version,uint32_t signature,uint32_t function,
    const void *payload,size_t payload_size,void *scratch,size_t scratch_size,
    uint32_t *out_sequence)
{
    size_t rpc_len,message_len,total_len,pages;
    uint8_t *msg=(uint8_t *)scratch;
    uint32_t read,write,used,free_pages,sequence;
    nvx_gsp_ring_notify_t notify; void *notify_context;
    if(!r||!scratch||!out_sequence||(payload_size&&!payload))
        return NVX_GSP_RING_INVALID_ARGUMENT;
    if(overlaps(r,sizeof(*r),scratch,scratch_size)||
       overlaps(r,sizeof(*r),out_sequence,sizeof(*out_sequence)))
        return NVX_GSP_RING_INVALID_ARGUMENT;
    if(!r->initialized) return NVX_GSP_RING_NOT_INITIALIZED;
    if(version!=NVX_GSP_RPC_HEADER_VERSION_R535||signature!=NVX_GSP_RPC_SIGNATURE_R535)
        return NVX_GSP_RING_BAD_MESSAGE;
    if(payload_size>NVX_GSP_MAX_RPC_PAYLOAD||
       payload_size>SIZE_MAX-NVX_GSP_RPC_HEADER_SIZE)
        return NVX_GSP_RING_MESSAGE_TOO_LARGE;
    rpc_len=NVX_GSP_RPC_HEADER_SIZE+payload_size;
    message_len=NVX_GSP_MSG_HEADER_SIZE+rpc_len;
    if(message_len>SIZE_MAX-(NVX_GSP_PAGE_SIZE-1U))
        return NVX_GSP_RING_MESSAGE_TOO_LARGE;
    total_len=(message_len+NVX_GSP_PAGE_SIZE-1U)&~((size_t)NVX_GSP_PAGE_SIZE-1U);
    pages=total_len/NVX_GSP_PAGE_SIZE;
    if(scratch_size<total_len) return NVX_GSP_RING_BUFFER_TOO_SMALL;
    if(overlaps(scratch,total_len,r->storage,(size_t)r->capacity_pages*NVX_GSP_PAGE_SIZE)||
       overlaps(scratch,total_len,payload,payload_size)||
       overlaps(scratch,total_len,(const void *)r->read_ptr,sizeof(*r->read_ptr))||
       overlaps(scratch,total_len,(const void *)r->write_ptr,sizeof(*r->write_ptr))||
       overlaps(out_sequence,sizeof(*out_sequence),scratch,total_len)||
       overlaps(out_sequence,sizeof(*out_sequence),r->storage,
                (size_t)r->capacity_pages*NVX_GSP_PAGE_SIZE)||
       overlaps(out_sequence,sizeof(*out_sequence),payload,payload_size)||
       overlaps(payload,payload_size,r,sizeof(*r))||
       overlaps(payload,payload_size,r->storage,
                (size_t)r->capacity_pages*NVX_GSP_PAGE_SIZE))
        return NVX_GSP_RING_INVALID_ARGUMENT;
    __atomic_store_n(out_sequence,0U,__ATOMIC_RELAXED);
    memset(msg,0,total_len);
    put32(msg+0U,NVX_GSP_MCTP_MAGIC);
    put32(msg+4U,(uint32_t)message_len);
    put32(msg+8U,NVX_GSP_MCTP_HEADER_SINGLE);
    put32(msg+12U,NVX_GSP_NVDM_HEADER_RM_RPC);
    put32(msg+16U,(uint32_t)rpc_len);
    put32(msg+20U,0U);
    put32(msg+24U,version);
    put32(msg+28U,signature);
    put32(msg+32U,(uint32_t)rpc_len);
    put32(msg+36U,function);
    put32(msg+40U,UINT32_MAX);
    put32(msg+44U,UINT32_MAX);
    put32(msg+48U,0U);
    put32(msg+52U,0U);
    if(payload_size) memcpy(msg+NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE,payload,payload_size);
    spinlock_lock(&r->lock);
    read=load_ptr(r->read_ptr); write=load_ptr(r->write_ptr);
    if(read>=r->capacity_pages||write>=r->capacity_pages) {
        spinlock_unlock(&r->lock); return NVX_GSP_RING_BAD_POINTER;
    }
    used=(uint32_t)(write-read);
    if(used>r->capacity_pages) {
        spinlock_unlock(&r->lock); return NVX_GSP_RING_BAD_POINTER;
    }
    free_pages=r->capacity_pages-used;
    if(pages>free_pages) { spinlock_unlock(&r->lock); return NVX_GSP_RING_QUEUE_FULL; }
    sequence=r->next_sequence++;
    if(r->next_sequence==0U) r->next_sequence=1U;
    /* The RPC wire sequence is the correlation key echoed by GSP replies. */
    put32(msg+48U,sequence);
    ring_copy_in(r,write%r->capacity_pages,msg,total_len);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(out_sequence,sequence,__ATOMIC_RELEASE);
    store_ptr(r->write_ptr,write+(uint32_t)pages);
    notify=r->notify; notify_context=r->notify_context;
    spinlock_unlock(&r->lock);
    if(notify) notify(notify_context);
    return NVX_GSP_RING_OK;
}
nvx_gsp_ring_status_t nvx_gsp_ring_receive(nvx_gsp_ring_t *r,void *scratch,
    size_t scratch_size,nvx_gsp_rpc_view_t *out)
{
    uint8_t header[NVX_GSP_MSG_HEADER_SIZE]; uint8_t *msg=(uint8_t *)scratch;
    uint32_t read,write,message_len,pages,rpc_len,mctp_header,nvdm_header;
    size_t total_len; nvx_gsp_ring_status_t status=NVX_GSP_RING_OK;
    if(!r||!scratch||!out) return NVX_GSP_RING_INVALID_ARGUMENT;
    if(overlaps(r,sizeof(*r),scratch,scratch_size)||
       overlaps(r,sizeof(*r),out,sizeof(*out))||
       overlaps(out,sizeof(*out),scratch,scratch_size))
        return NVX_GSP_RING_INVALID_ARGUMENT;
    if(!r->initialized) return NVX_GSP_RING_NOT_INITIALIZED;
    if(overlaps(out,sizeof(*out),r->storage,(size_t)r->capacity_pages*NVX_GSP_PAGE_SIZE)||
       overlaps(scratch,scratch_size,(const void *)r->read_ptr,sizeof(*r->read_ptr))||
       overlaps(scratch,scratch_size,(const void *)r->write_ptr,sizeof(*r->write_ptr))||
       overlaps(out,sizeof(*out),(const void *)r->read_ptr,sizeof(*r->read_ptr))||
       overlaps(out,sizeof(*out),(const void *)r->write_ptr,sizeof(*r->write_ptr)))
        return NVX_GSP_RING_INVALID_ARGUMENT;
    if(overlaps(scratch,scratch_size,r->storage,(size_t)r->capacity_pages*NVX_GSP_PAGE_SIZE))
        return NVX_GSP_RING_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    spinlock_lock(&r->lock);
    read=load_ptr(r->read_ptr); write=load_ptr(r->write_ptr);
    uint32_t available=(uint32_t)(write-read);
    if(available>r->capacity_pages) status=NVX_GSP_RING_BAD_POINTER;
    else if(available==0U) status=NVX_GSP_RING_QUEUE_EMPTY;
    if(status!=NVX_GSP_RING_OK) { spinlock_unlock(&r->lock); return status; }
    ring_copy_out(r,read%r->capacity_pages,header,sizeof(header));
    message_len=get32(header+4U);
    if(get32(header+0U)!=NVX_GSP_MCTP_MAGIC||
       get32(header+8U)!=NVX_GSP_MCTP_HEADER_SINGLE||
       get32(header+12U)!=NVX_GSP_NVDM_HEADER_RM_RPC||
       message_len<NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE||
       message_len>NVX_GSP_MAX_MESSAGE_PAGES*NVX_GSP_PAGE_SIZE) {
        spinlock_unlock(&r->lock); return NVX_GSP_RING_BAD_MESSAGE;
    }
    pages=(message_len+NVX_GSP_PAGE_SIZE-1U)/NVX_GSP_PAGE_SIZE;
    if(!pages||pages>NVX_GSP_MAX_MESSAGE_PAGES||pages>=r->capacity_pages||
       available<pages) {
        spinlock_unlock(&r->lock); return NVX_GSP_RING_BAD_MESSAGE;
    }
    total_len=(size_t)pages*NVX_GSP_PAGE_SIZE;
    if(scratch_size<total_len) { spinlock_unlock(&r->lock); return NVX_GSP_RING_BUFFER_TOO_SMALL; }
    ring_copy_out(r,read%r->capacity_pages,msg,total_len);
    rpc_len=get32(msg+16U);
    mctp_header=get32(msg+8U); nvdm_header=get32(msg+12U);
    if(get32(msg+20U)!=0U||rpc_len!=message_len-NVX_GSP_MSG_HEADER_SIZE||
       get32(msg+32U)!=rpc_len||
       get32(msg+24U)!=NVX_GSP_RPC_HEADER_VERSION_R535||
       get32(msg+28U)!=NVX_GSP_RPC_SIGNATURE_R535||
       (mctp_header&0xFU)!=1U||((mctp_header>>31U)&1U)!=1U||
       ((mctp_header>>30U)&1U)!=1U||
       (nvdm_header&0xFFU)!=0x7EU||
       ((nvdm_header>>8U)&0xFFFFU)!=0x10DEU||
       ((nvdm_header>>24U)&0xFFU)!=NVX_GSP_NVDM_TYPE_RM_RPC) {
        spinlock_unlock(&r->lock); return NVX_GSP_RING_BAD_MESSAGE;
    }
    out->header_version=get32(msg+24U); out->signature=get32(msg+28U);
    out->length=rpc_len; out->function=get32(msg+36U);
    out->result=get32(msg+40U); out->result_private=get32(msg+44U);
    out->sequence=get32(msg+48U); out->spare=get32(msg+52U);
    out->payload=msg+NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE;
    out->payload_size=rpc_len-NVX_GSP_RPC_HEADER_SIZE;
    out->message_sequence=(mctp_header>>28U)&0x3U;
    out->element_pages=pages;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    store_ptr(r->read_ptr,read+pages);
    spinlock_unlock(&r->lock);
    return NVX_GSP_RING_OK;
}
