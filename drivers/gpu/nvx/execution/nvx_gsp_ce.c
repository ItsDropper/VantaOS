/* Copyright (c) 2026 ItsDropper. MIT License; see LICENSE. */
#include "nvx_gsp_ce.h"
#include <stdint.h>
#include <limits.h>

static int add_u64(uint64_t a,uint64_t b,uint64_t *out)
{
    if(!out||a>UINT64_MAX-b)return 0;
    *out=a+b;
    return 1;
}
static int overlaps(const volatile void *a,size_t an,const volatile void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!an||!bn)return 0;
    if(!a||!b||x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn)return 1;
    return x<y+bn&&y<x+an;
}
static uint32_t method_header(uint32_t method,uint32_t count,uint32_t subch)
{
    return (UINT32_C(1)<<29U)|(count<<16U)|(subch<<13U)|((method>>2U)&0xFFFU);
}
static void emit(volatile uint32_t *p,size_t *n,uint32_t v)
{
    __atomic_store_n(&p[*n],v,__ATOMIC_RELAXED);
    ++*n;
}

nvx_gsp_ce_status_t nvx_gsp_ce_build_copy(
    volatile uint32_t *buffer,size_t capacity,uint32_t object,uint32_t subchannel,
    uint64_t src,uint64_t dst,uint64_t completion,uint32_t bytes,
    uint32_t completion_value,size_t *out_bytes)
{
    uint64_t src_end,dst_end,completion_end;
    size_t n=0U;
    const uint32_t launch_dma=UINT32_C(0x18E);
    if(out_bytes)*out_bytes=0U;
    if(!buffer||!out_bytes||!object||subchannel>7U||
       ((uintptr_t)buffer&3U)!=0U)
        return NVX_GSP_CE_INVALID_ARGUMENT;
    if(capacity<NVX_GSP_CE_COPY_COMMAND_BYTES)return NVX_GSP_CE_NO_SPACE;
    if(!bytes||(bytes&3U)!=0U||(src&3U)!=0U||(dst&3U)!=0U||
       (completion&3U)!=0U||src>=NVX_GSP_CE_GPUVA_LIMIT||
       dst>=NVX_GSP_CE_GPUVA_LIMIT||completion>=NVX_GSP_CE_GPUVA_LIMIT||
       !add_u64(src,bytes,&src_end)||!add_u64(dst,bytes,&dst_end)||
       !add_u64(completion,sizeof(uint32_t),&completion_end)||
       src_end>NVX_GSP_CE_GPUVA_LIMIT||dst_end>NVX_GSP_CE_GPUVA_LIMIT||
       completion_end>NVX_GSP_CE_GPUVA_LIMIT)
        return NVX_GSP_CE_BAD_GPU_ADDRESS;
    if(overlaps(buffer,NVX_GSP_CE_COPY_COMMAND_BYTES,out_bytes,sizeof(*out_bytes)))
        return NVX_GSP_CE_INVALID_ARGUMENT;

    /* Select the allocated NVC5B5 copy-engine object on the requested subchannel. */
    emit(buffer,&n,method_header(0x000U,1U,subchannel));
    emit(buffer,&n,object);

    /* Configure one-word semaphore release, issued after the copy completes. */
    emit(buffer,&n,method_header(0x240U,3U,subchannel));
    emit(buffer,&n,(uint32_t)(completion>>32U)&UINT32_C(0x1FFFF));
    emit(buffer,&n,(uint32_t)completion);
    emit(buffer,&n,completion_value);

    /* NVC5B5 source/destination address methods, 49-bit GPU virtual addresses. */
    emit(buffer,&n,method_header(0x400U,4U,subchannel));
    emit(buffer,&n,(uint32_t)(src>>32U)&UINT32_C(0x1FFFF));
    emit(buffer,&n,(uint32_t)src);
    emit(buffer,&n,(uint32_t)(dst>>32U)&UINT32_C(0x1FFFF));
    emit(buffer,&n,(uint32_t)dst);

    /* One pitch-layout line; length is bytes and line count is one. */
    emit(buffer,&n,method_header(0x418U,2U,subchannel));
    emit(buffer,&n,bytes);
    emit(buffer,&n,1U);

    emit(buffer,&n,method_header(0x300U,1U,subchannel));
    emit(buffer,&n,launch_dma);
    if(n!=NVX_GSP_CE_COPY_COMMAND_DWORDS)return NVX_GSP_CE_INVALID_ARGUMENT;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *out_bytes=NVX_GSP_CE_COPY_COMMAND_BYTES;
    return NVX_GSP_CE_OK;
}
