/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_msgq_rpc.h"
#include <string.h>
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U);
}
static void put32(uint8_t *p,uint32_t v)
{
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8U);p[2]=(uint8_t)(v>>16U);p[3]=(uint8_t)(v>>24U);
}
static int overlaps(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!an||!bn)return 0;
    if(x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn)return 1;
    return x<y+bn&&y<x+an;
}
nvx_gsp_msgq_rpc_status_t nvx_gsp_msgq_rpc_init(nvx_gsp_msgq_rpc_transport_t *t,
    nvx_gsp_msgq_queue_t *tx,nvx_gsp_msgq_queue_t *rx)
{
    if(!t||!tx||!rx)return NVX_GSP_MSGQ_RPC_INVALID_ARGUMENT;
    if(overlaps(t,sizeof(*t),tx,sizeof(*tx))||
       overlaps(t,sizeof(*t),rx,sizeof(*rx))||
       overlaps(tx,sizeof(*tx),rx,sizeof(*rx)))
        return NVX_GSP_MSGQ_RPC_INVALID_ARGUMENT;
    if(t->initialized)return NVX_GSP_MSGQ_RPC_INVALID_ARGUMENT;
    if(!tx->initialized||!rx->initialized)return NVX_GSP_MSGQ_RPC_NOT_INITIALIZED;
    if(!tx->transmit||rx->transmit||tx->layout.element_size!=NVX_GSP_PAGE_SIZE||
       rx->layout.element_size!=NVX_GSP_PAGE_SIZE||
       overlaps(tx->backing_store,tx->layout.backing_store_size,
                rx->backing_store,rx->layout.backing_store_size))
        return NVX_GSP_MSGQ_RPC_INVALID_ARGUMENT;
    t->command_queue=tx;t->response_queue=rx;t->next_sequence=1U;t->initialized=1U;
    return NVX_GSP_MSGQ_RPC_OK;
}
nvx_gsp_msgq_rpc_status_t nvx_gsp_msgq_rpc_send(nvx_gsp_msgq_rpc_transport_t *t,
    uint32_t function,const void *payload,size_t payload_size,void *scratch,
    size_t scratch_size,uint32_t *out_sequence)
{
    size_t rpc_len,message_len,slots,total;uint32_t sequence;
    uint8_t *msg=(uint8_t *)scratch;nvx_gsp_msgq_queue_status_t qs;
    if(!t||!scratch||!out_sequence||(payload_size&&!payload))
        return NVX_GSP_MSGQ_RPC_INVALID_ARGUMENT;
    if(!t->initialized)return NVX_GSP_MSGQ_RPC_NOT_INITIALIZED;
    if(overlaps(t,sizeof(*t),scratch,scratch_size)||
       overlaps(t,sizeof(*t),out_sequence,sizeof(*out_sequence))||
       overlaps(t,sizeof(*t),payload,payload_size)||
       overlaps(out_sequence,sizeof(*out_sequence),payload,payload_size)||
       overlaps(scratch,scratch_size,t->response_queue->backing_store,
                t->response_queue->layout.backing_store_size)||
       overlaps(out_sequence,sizeof(*out_sequence),t->response_queue->backing_store,
                t->response_queue->layout.backing_store_size)||
       overlaps(scratch,scratch_size,payload,payload_size)||
       overlaps(scratch,scratch_size,t->command_queue->backing_store,
                t->command_queue->layout.backing_store_size)||
       overlaps(out_sequence,sizeof(*out_sequence),scratch,scratch_size)||
       overlaps(out_sequence,sizeof(*out_sequence),t->command_queue->backing_store,
                t->command_queue->layout.backing_store_size))
        return NVX_GSP_MSGQ_RPC_INVALID_ARGUMENT;
    *out_sequence=0U;
    if(payload_size>SIZE_MAX-NVX_GSP_MSG_HEADER_SIZE-NVX_GSP_RPC_HEADER_SIZE)
        return NVX_GSP_MSGQ_RPC_BUFFER_TOO_SMALL;
    rpc_len=NVX_GSP_RPC_HEADER_SIZE+payload_size;
    message_len=NVX_GSP_MSG_HEADER_SIZE+rpc_len;
    if(message_len>NVX_GSP_MSGQ_RPC_MAX_ELEMENTS*NVX_GSP_PAGE_SIZE)
        return NVX_GSP_MSGQ_RPC_BUFFER_TOO_SMALL;
    slots=(message_len+NVX_GSP_PAGE_SIZE-1U)/NVX_GSP_PAGE_SIZE;
    total=slots*NVX_GSP_PAGE_SIZE;
    if(scratch_size<total)return NVX_GSP_MSGQ_RPC_BUFFER_TOO_SMALL;
    memset(msg,0,total);
    put32(msg,NVX_GSP_MCTP_MAGIC);put32(msg+4U,(uint32_t)message_len);
    put32(msg+8U,NVX_GSP_MCTP_HEADER_SINGLE);put32(msg+12U,NVX_GSP_NVDM_HEADER_RM_RPC);
    put32(msg+16U,(uint32_t)rpc_len);put32(msg+20U,0U);
    put32(msg+24U,NVX_GSP_RPC_HEADER_VERSION_R535);put32(msg+28U,NVX_GSP_RPC_SIGNATURE_R535);
    put32(msg+32U,(uint32_t)rpc_len);put32(msg+36U,function);
    put32(msg+40U,UINT32_MAX);put32(msg+44U,UINT32_MAX);
    sequence=t->next_sequence;if(!sequence)sequence=1U;
    put32(msg+48U,sequence);put32(msg+52U,0U);
    if(payload_size)memcpy(msg+NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE,payload,payload_size);
    for(size_t i=0;i<slots;i++) {
        uint8_t *element=0;
        qs=nvx_gsp_msgq_tx_get(t->command_queue,(uint32_t)i,&element);
        if(qs!=NVX_GSP_MSGQ_QUEUE_OK)
            return qs==NVX_GSP_MSGQ_QUEUE_FULL?NVX_GSP_MSGQ_RPC_QUEUE_FULL:NVX_GSP_MSGQ_RPC_QUEUE_ERROR;
        memcpy(element,msg+i*NVX_GSP_PAGE_SIZE,NVX_GSP_PAGE_SIZE);
    }
    /*
     * Publish the correlation ID before advancing the hardware-visible head.
     * An immediate GSP reply must never race a still-zero manager slot sequence.
     */
    __atomic_store_n(out_sequence,sequence,__ATOMIC_RELEASE);
    qs=nvx_gsp_msgq_tx_submit(t->command_queue,(uint32_t)slots);
    if(qs!=NVX_GSP_MSGQ_QUEUE_OK) {
        __atomic_store_n(out_sequence,0U,__ATOMIC_RELEASE);
        return qs==NVX_GSP_MSGQ_QUEUE_FULL?NVX_GSP_MSGQ_RPC_QUEUE_FULL:NVX_GSP_MSGQ_RPC_QUEUE_ERROR;
    }
    t->next_sequence=sequence+1U;if(!t->next_sequence)t->next_sequence=1U;
    return NVX_GSP_MSGQ_RPC_OK;
}
nvx_gsp_msgq_rpc_status_t nvx_gsp_msgq_rpc_receive(nvx_gsp_msgq_rpc_transport_t *t,
    void *scratch,size_t scratch_size,nvx_gsp_rpc_view_t *out)
{
    const uint8_t *first=0;uint8_t *msg=(uint8_t *)scratch;
    uint32_t message_len,rpc_len,mctp,nvdm,slots;size_t total;
    nvx_gsp_msgq_queue_status_t qs;
    if(!t||!scratch||!out)return NVX_GSP_MSGQ_RPC_INVALID_ARGUMENT;
    if(!t->initialized)return NVX_GSP_MSGQ_RPC_NOT_INITIALIZED;
    if(overlaps(t,sizeof(*t),scratch,scratch_size)||
       overlaps(out,sizeof(*out),t,sizeof(*t))||
       overlaps(out,sizeof(*out),scratch,scratch_size)||
       overlaps(out,sizeof(*out),t->response_queue->backing_store,
                t->response_queue->layout.backing_store_size)||
       overlaps(scratch,scratch_size,t->response_queue->backing_store,
                t->response_queue->layout.backing_store_size))
        return NVX_GSP_MSGQ_RPC_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    qs=nvx_gsp_msgq_rx_get(t->response_queue,0U,&first);
    if(qs==NVX_GSP_MSGQ_QUEUE_EMPTY)return NVX_GSP_MSGQ_RPC_QUEUE_EMPTY;
    if(qs!=NVX_GSP_MSGQ_QUEUE_OK)return NVX_GSP_MSGQ_RPC_QUEUE_ERROR;
    message_len=get32(first+4U);
    if(get32(first)!=NVX_GSP_MCTP_MAGIC||
       message_len<NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE||
       message_len>NVX_GSP_MSGQ_RPC_MAX_ELEMENTS*NVX_GSP_PAGE_SIZE)
        return NVX_GSP_MSGQ_RPC_BAD_MESSAGE;
    slots=(message_len+NVX_GSP_PAGE_SIZE-1U)/NVX_GSP_PAGE_SIZE;
    total=(size_t)slots*NVX_GSP_PAGE_SIZE;
    if(scratch_size<total)return NVX_GSP_MSGQ_RPC_BUFFER_TOO_SMALL;
    for(uint32_t i=0;i<slots;i++) {
        const uint8_t *element=0;
        qs=nvx_gsp_msgq_rx_get(t->response_queue,i,&element);
        if(qs==NVX_GSP_MSGQ_QUEUE_EMPTY)return NVX_GSP_MSGQ_RPC_QUEUE_EMPTY;
        if(qs!=NVX_GSP_MSGQ_QUEUE_OK)return NVX_GSP_MSGQ_RPC_QUEUE_ERROR;
        memcpy(msg+(size_t)i*NVX_GSP_PAGE_SIZE,element,NVX_GSP_PAGE_SIZE);
    }
    rpc_len=get32(msg+16U);mctp=get32(msg+8U);nvdm=get32(msg+12U);
    if(get32(msg+20U)!=0U||rpc_len!=message_len-NVX_GSP_MSG_HEADER_SIZE||
       get32(msg+24U)!=NVX_GSP_RPC_HEADER_VERSION_R535||
       get32(msg+28U)!=NVX_GSP_RPC_SIGNATURE_R535||get32(msg+32U)!=rpc_len||
       (mctp&0xFU)!=1U||((mctp>>31U)&1U)!=1U||((mctp>>30U)&1U)!=1U||
       (nvdm&0xFFU)!=0x7EU||((nvdm>>8U)&0xFFFFU)!=0x10DEU||
       ((nvdm>>24U)&0xFFU)!=NVX_GSP_NVDM_TYPE_RM_RPC)
        return NVX_GSP_MSGQ_RPC_BAD_MESSAGE;
    out->header_version=get32(msg+24U);out->signature=get32(msg+28U);
    out->length=rpc_len;out->function=get32(msg+36U);
    out->result=get32(msg+40U);out->result_private=get32(msg+44U);
    out->sequence=get32(msg+48U);out->spare=get32(msg+52U);
    out->payload=msg+NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE;
    out->payload_size=rpc_len-NVX_GSP_RPC_HEADER_SIZE;
    out->message_sequence=(mctp>>28U)&0x3U;out->element_pages=slots;
    qs=nvx_gsp_msgq_rx_consume(t->response_queue,slots);
    if(qs!=NVX_GSP_MSGQ_QUEUE_OK){memset(out,0,sizeof(*out));return NVX_GSP_MSGQ_RPC_QUEUE_ERROR;}
    return NVX_GSP_MSGQ_RPC_OK;
}
