/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_msgq.h"
#include <string.h>
static uint16_t get16(const uint8_t *p) { return (uint16_t)p[0]|((uint16_t)p[1]<<8U); }
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U);
}
static void put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8U); }
static void put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8U); p[2]=(uint8_t)(v>>16U); p[3]=(uint8_t)(v>>24U);
}
static int aligned_uintptr(const void *p,size_t a) { return ((uintptr_t)p&(a-1U))==0U; }

static nvx_gsp_msgq_status_t decode(const void *store,size_t bytes,size_t elem,
    nvx_gsp_msgq_layout_t *out,int check_reserved)
{
    const uint8_t *p=(const uint8_t *)store;
    uint32_t total,msg_size,count,off;
    if(!store||!out) return NVX_GSP_MSGQ_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!aligned_uintptr(store,NVX_GSP_MSGQ_HEADER_PAGE_SIZE))
        return NVX_GSP_MSGQ_BAD_ALIGNMENT;
    if(bytes>UINT32_MAX||bytes<2U*NVX_GSP_MSGQ_HEADER_PAGE_SIZE||
       elem<NVX_GSP_MSGQ_MIN_ELEMENT_SIZE||
       elem>NVX_GSP_MSGQ_MAX_ELEMENT_SIZE||(elem&(elem-1U))!=0U||
       bytes%NVX_GSP_MSGQ_ELEMENT_SIZE!=0U)
        return NVX_GSP_MSGQ_BAD_SIZE;
    if(get16(p)!=NVX_GSP_MSGQ_VERSION_MAJOR) return NVX_GSP_MSGQ_BAD_VERSION;
    total=get32(p+4U); msg_size=get32(p+8U); count=get32(p+12U); off=get32(p+16U);
    if(total!=bytes||msg_size!=elem||off<NVX_GSP_MSGQ_HEADER_PAGE_SIZE||
       off%elem!=0U||off>total||total-off<msg_size||
       count!=(total-off)/msg_size||count<1U||count>NVX_GSP_MSGQ_MAX_ELEMENTS)
        return NVX_GSP_MSGQ_BAD_LAYOUT;
    if(check_reserved&&(get32(p+20U)!=0U||get32(p+24U)!=0U||get32(p+28U)!=0U))
        return NVX_GSP_MSGQ_BAD_RESERVED;
    out->entries=(uint8_t *)store+off;
    out->backing_store_size=total; out->entry_offset=off;
    out->element_size=msg_size; out->element_count=count;
    return NVX_GSP_MSGQ_OK;
}
nvx_gsp_msgq_status_t nvx_gsp_msgq_prepare_tx(void *store,size_t bytes,size_t elem,
    nvx_gsp_msgq_layout_t *out)
{
    uint32_t off,count;
    if(!out) return NVX_GSP_MSGQ_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!store) return NVX_GSP_MSGQ_INVALID_ARGUMENT;
    if(!aligned_uintptr(store,NVX_GSP_MSGQ_HEADER_PAGE_SIZE))
        return NVX_GSP_MSGQ_BAD_ALIGNMENT;
    if(bytes>UINT32_MAX||bytes<2U*NVX_GSP_MSGQ_HEADER_PAGE_SIZE||
       elem<NVX_GSP_MSGQ_MIN_ELEMENT_SIZE||
       elem>NVX_GSP_MSGQ_MAX_ELEMENT_SIZE||(elem&(elem-1U))!=0U||
       bytes%NVX_GSP_MSGQ_ELEMENT_SIZE!=0U)
        return NVX_GSP_MSGQ_BAD_SIZE;
    off=NVX_GSP_MSGQ_HEADER_PAGE_SIZE;
    count=(uint32_t)((bytes-off)/elem);
    if(count<1U||count>NVX_GSP_MSGQ_MAX_ELEMENTS) return NVX_GSP_MSGQ_BAD_LAYOUT;
    memset(store,0,off);
    uint8_t *p=(uint8_t *)store;
    put16(p,NVX_GSP_MSGQ_VERSION_MAJOR); put16(p+2U,NVX_GSP_MSGQ_VERSION_MINOR);
    put32(p+4U,(uint32_t)bytes); put32(p+8U,(uint32_t)elem);
    put32(p+12U,count); put32(p+16U,off);
    out->entries=p+off; out->backing_store_size=(uint32_t)bytes;
    out->entry_offset=off; out->element_size=(uint32_t)elem; out->element_count=count;
    return NVX_GSP_MSGQ_OK;
}
nvx_gsp_msgq_status_t nvx_gsp_msgq_validate_rx(const void *store,size_t bytes,
    size_t elem,nvx_gsp_msgq_layout_t *out)
{
    return decode(store,bytes,elem,out,1);
}
