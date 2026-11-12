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
static int gpu_ranges_overlap(uint64_t a,uint64_t a_bytes,
                               uint64_t b,uint64_t b_bytes)
{
    uint64_t a_end,b_end;
    if(!a_bytes||!b_bytes)return 0;
    if(!add_u64(a,a_bytes,&a_end)||!add_u64(b,b_bytes,&b_end))
        return 1;
    return a<b_end&&b<a_end;
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

static int supported_copy_class(uint32_t class_id)
{
    return class_id==NVX_GSP_CE_TURING_DMA_COPY_A_CLASS||
           class_id==NVX_GSP_CE_AMPERE_DMA_COPY_A_CLASS||
           class_id==NVX_GSP_CE_AMPERE_DMA_COPY_B_CLASS;
}

nvx_gsp_ce_status_t nvx_gsp_ce_build_copy(
    volatile uint32_t *buffer,size_t capacity,uint32_t copy_class,uint32_t subchannel,
    uint64_t src,uint64_t dst,uint64_t completion,uint32_t bytes,
    uint32_t completion_value,size_t *out_bytes)
{
    uint64_t src_end,dst_end,completion_end;
    size_t n=0U;
    const uint32_t launch_dma=NVX_GSP_CE_LAUNCH_DMA_FLAGS;
    if(out_bytes)*out_bytes=0U;
    if(!buffer||!out_bytes||!supported_copy_class(copy_class)||subchannel>7U||
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
    if((src<dst_end&&dst<src_end)||
       (completion<src_end&&src<completion_end)||
       (completion<dst_end&&dst<completion_end))
        return NVX_GSP_CE_BAD_GPU_ADDRESS;
    if(overlaps(buffer,NVX_GSP_CE_COPY_COMMAND_BYTES,out_bytes,sizeof(*out_bytes)))
        return NVX_GSP_CE_INVALID_ARGUMENT;

    /*
     * SET_OBJECT: NVCLASS (bits 15:0) is the copy-engine class ID and ENGINE
     * (bits 20:16) is 0 for a normal engine class. The RM object handle is
     * never placed in the pushbuffer.
     */
    emit(buffer,&n,method_header(0x000U,1U,subchannel));
    emit(buffer,&n,copy_class&UINT32_C(0xFFFF));

    /* Configure one-word semaphore release, issued after the copy completes. */
    emit(buffer,&n,method_header(0x240U,3U,subchannel));
    emit(buffer,&n,(uint32_t)(completion>>32U)&UINT32_C(0x1FFFF));
    emit(buffer,&n,(uint32_t)completion);
    emit(buffer,&n,completion_value);

    /* Source/destination address methods (NVC5B5/6B5/7B5 offsets are identical). */
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

nvx_gsp_ce_runtime_status_t nvx_gsp_ce_copy_submit_and_wait(
    nvx_gsp_gpfifo_channel_t *channel,
    volatile uint32_t *command_buffer,size_t command_capacity_bytes,
    uint64_t command_gpu_va,uint32_t copy_class,uint32_t subchannel,
    uint64_t source_gpu_va,uint64_t destination_gpu_va,
    uint64_t completion_gpu_va,volatile uint32_t *completion_cpu_word,
    uint32_t bytes,uint32_t completion_value,
    uint32_t timeout_ms,uint32_t poll_interval_ms,
    nvx_gsp_gpfifo_delay_fn delay_ms,void *context,
    size_t *out_command_bytes,uint32_t *out_put_index,
    nvx_gsp_gpfifo_status_t *out_fifo_status)
{
    nvx_gsp_ce_status_t build_status;
    nvx_gsp_gpfifo_status_t fifo_status;
    uint64_t command_end;
    size_t command_bytes=0U;

    if(!channel||!command_buffer||!completion_cpu_word||
       !out_command_bytes||!out_put_index||!timeout_ms||
       !poll_interval_ms||!delay_ms||
       ((uintptr_t)command_buffer&3U)!=0U||
       ((uintptr_t)completion_cpu_word&3U)!=0U||
       overlaps(command_buffer,command_capacity_bytes,
                completion_cpu_word,sizeof(*completion_cpu_word))||
       overlaps(command_buffer,command_capacity_bytes,channel,
                sizeof(*channel))||
       overlaps(completion_cpu_word,sizeof(*completion_cpu_word),channel,
                sizeof(*channel))||
       overlaps(out_command_bytes,sizeof(*out_command_bytes),
                out_put_index,sizeof(*out_put_index))||
       overlaps(out_command_bytes,sizeof(*out_command_bytes),
                channel,sizeof(*channel))||
       overlaps(out_put_index,sizeof(*out_put_index),
                channel,sizeof(*channel))||
       overlaps(out_command_bytes,sizeof(*out_command_bytes),
                command_buffer,command_capacity_bytes)||
       overlaps(out_put_index,sizeof(*out_put_index),
                command_buffer,command_capacity_bytes)||
       overlaps(out_command_bytes,sizeof(*out_command_bytes),
                completion_cpu_word,sizeof(*completion_cpu_word))||
       overlaps(out_put_index,sizeof(*out_put_index),
                completion_cpu_word,sizeof(*completion_cpu_word))||
       (out_fifo_status&&
        (overlaps(out_fifo_status,sizeof(*out_fifo_status),
                  out_command_bytes,sizeof(*out_command_bytes))||
         overlaps(out_fifo_status,sizeof(*out_fifo_status),
                  out_put_index,sizeof(*out_put_index))||
         overlaps(out_fifo_status,sizeof(*out_fifo_status),channel,
                  sizeof(*channel))||
         overlaps(out_fifo_status,sizeof(*out_fifo_status),command_buffer,
                  command_capacity_bytes)||
         overlaps(out_fifo_status,sizeof(*out_fifo_status),
                  completion_cpu_word,sizeof(*completion_cpu_word)))))
        return NVX_GSP_CE_RUNTIME_INVALID_ARGUMENT;

    *out_command_bytes=0U;
    *out_put_index=0U;
    if(out_fifo_status)*out_fifo_status=NVX_GSP_GPFIFO_INVALID_ARGUMENT;

    /*
     * The pushbuffer fetcher has a 40-bit address window. Check the entire
     * fixed-size command range before building or publishing any work.
     */
    if(command_gpu_va>=NVX_GSP_GPFIFO_ADDRESS_LIMIT||
       (command_gpu_va&3U)!=0U||
       !add_u64(command_gpu_va,NVX_GSP_CE_COPY_COMMAND_BYTES,&command_end)||
       command_end>NVX_GSP_GPFIFO_ADDRESS_LIMIT)
        return NVX_GSP_CE_RUNTIME_BAD_GPU_RANGE;

    /*
     * Requiring a fresh completion value prevents a stale semaphore from
     * satisfying the wait before this transaction's command has executed.
     * The caller owns cache maintenance and must choose a unique value.
     */
    if(__atomic_load_n(completion_cpu_word,__ATOMIC_ACQUIRE)==completion_value)
        return NVX_GSP_CE_RUNTIME_STALE_COMPLETION;

    build_status=nvx_gsp_ce_build_copy(command_buffer,command_capacity_bytes,
        copy_class,subchannel,source_gpu_va,destination_gpu_va,
        completion_gpu_va,bytes,completion_value,&command_bytes);
    if(build_status!=NVX_GSP_CE_OK)
        return NVX_GSP_CE_RUNTIME_BUILD_FAILED;

    /*
     * The command stream must not alias any payload or semaphore it references:
     * the GPU fetcher could otherwise read bytes while the CE overwrites them.
     * The builder has already range-checked all payload intervals.
     */
    if(gpu_ranges_overlap(command_gpu_va,command_bytes,source_gpu_va,bytes)||
       gpu_ranges_overlap(command_gpu_va,command_bytes,
                          destination_gpu_va,bytes)||
       gpu_ranges_overlap(command_gpu_va,command_bytes,
                          completion_gpu_va,sizeof(uint32_t)))
        return NVX_GSP_CE_RUNTIME_BAD_GPU_RANGE;

    *out_command_bytes=command_bytes;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    fifo_status=nvx_gsp_gpfifo_submit_and_wait(channel,command_gpu_va,
        command_bytes,completion_cpu_word,completion_value,timeout_ms,
        poll_interval_ms,delay_ms,context,out_put_index);
    if(out_fifo_status)*out_fifo_status=fifo_status;
    if(fifo_status==NVX_GSP_GPFIFO_OK)
        return NVX_GSP_CE_RUNTIME_OK;
    if(fifo_status==NVX_GSP_GPFIFO_TIMEOUT)
        return NVX_GSP_CE_RUNTIME_TIMEOUT;
    return NVX_GSP_CE_RUNTIME_SUBMIT_FAILED;
}
