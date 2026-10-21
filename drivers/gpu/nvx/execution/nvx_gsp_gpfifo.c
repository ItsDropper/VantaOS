/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_gpfifo.h"

#include <stdint.h>
#include <limits.h>

static int add_u64(uint64_t a,uint64_t b,uint64_t *out)
{
    if(!out||a>UINT64_MAX-b)return 0;
    *out=a+b;
    return 1;
}
static int power_of_two(uint32_t n)
{
    return n>=2U&&(n&(n-1U))==0U;
}
static int overlaps(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!an||!bn)return 0;
    if(!a||!b||x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn)return 1;
    return x<y+bn&&y<x+an;
}

nvx_gsp_gpfifo_status_t nvx_gsp_gpfifo_channel_init(
    nvx_gsp_gpfifo_channel_t *c,volatile uint32_t *entries,
    size_t mapped_bytes,volatile uint32_t *gp_get,volatile uint32_t *gp_put,
    volatile uint32_t *doorbell,uint32_t token,uint32_t count)
{
    size_t needed;
    uint32_t put;
    if(!c||!entries||!gp_get||!gp_put||!doorbell||
       ((uintptr_t)entries&3U)!=0U||((uintptr_t)gp_get&3U)!=0U||
       ((uintptr_t)gp_put&3U)!=0U||((uintptr_t)doorbell&3U)!=0U||
       !power_of_two(count))
        return NVX_GSP_GPFIFO_INVALID_ARGUMENT;
    needed=(size_t)count*NVX_GSP_GPFIFO_ENTRY_DWORDS*4U;
    if(mapped_bytes<needed)return NVX_GSP_GPFIFO_BAD_RING;
    if(overlaps(c,sizeof(*c),entries,needed)||
       overlaps(c,sizeof(*c),gp_get,sizeof(*gp_get))||
       overlaps(c,sizeof(*c),gp_put,sizeof(*gp_put))||
       overlaps(c,sizeof(*c),doorbell,sizeof(*doorbell))||
       overlaps(entries,needed,gp_get,sizeof(*gp_get))||
       overlaps(entries,needed,gp_put,sizeof(*gp_put))||
       overlaps(entries,needed,doorbell,sizeof(*doorbell))||
       overlaps(gp_get,sizeof(*gp_get),gp_put,sizeof(*gp_put))||
       overlaps(gp_get,sizeof(*gp_get),doorbell,sizeof(*doorbell))||
       overlaps(gp_put,sizeof(*gp_put),doorbell,sizeof(*doorbell)))
        return NVX_GSP_GPFIFO_INVALID_ARGUMENT;
    put=__atomic_load_n(gp_put,__ATOMIC_ACQUIRE);
    if(put>=count||__atomic_load_n(gp_get,__ATOMIC_ACQUIRE)>=count)
        return NVX_GSP_GPFIFO_BAD_REGISTER_STATE;
    c->entries=entries;c->entries_mapping_bytes=mapped_bytes;
    c->gp_get=gp_get;c->gp_put=gp_put;c->doorbell=doorbell;
    c->doorbell_token=token;c->entry_count=count;c->put_index=put;
    c->initialized=1U;
    return NVX_GSP_GPFIFO_OK;
}

nvx_gsp_gpfifo_status_t nvx_gsp_gpfifo_submit(
    nvx_gsp_gpfifo_channel_t *c,uint64_t command_va,size_t command_bytes,
    uint32_t *out_put)
{
    uint64_t end;
    uint32_t get,next,dwords,entry0,entry1;
    size_t slot;
    if(out_put)*out_put=0U;
    if(!c||!out_put||!c->initialized||!c->entries||!c->gp_get||
       !c->gp_put||!c->doorbell)
        return NVX_GSP_GPFIFO_NOT_INITIALIZED;
    if(command_bytes==0U||(command_bytes&3U)!=0U||
       command_bytes/4U>NVX_GSP_GPFIFO_MAX_LENGTH_DWORDS||
       command_va>=NVX_GSP_GPFIFO_ADDRESS_LIMIT||
       (command_va&3U)!=0U||
       !add_u64(command_va,(uint64_t)command_bytes,&end)||
       end>NVX_GSP_GPFIFO_ADDRESS_LIMIT)
        return NVX_GSP_GPFIFO_BAD_COMMAND_RANGE;
    get=__atomic_load_n(c->gp_get,__ATOMIC_ACQUIRE);
    if(get>=c->entry_count||c->put_index>=c->entry_count)
        return NVX_GSP_GPFIFO_BAD_REGISTER_STATE;
    next=(c->put_index+1U)&(c->entry_count-1U);
    if(next==get)return NVX_GSP_GPFIFO_QUEUE_FULL;

    dwords=(uint32_t)(command_bytes/4U);
    /* Public NVC56F GP_ENTRY0/1 fields: address>>2, high address, length>>2. */
    entry0=(uint32_t)((command_va>>2U)&UINT64_C(0x3FFFFFFF));
    entry1=(uint32_t)((command_va>>32U)&UINT64_C(0xFF))|(dwords<<10U);
    slot=(size_t)c->put_index*NVX_GSP_GPFIFO_ENTRY_DWORDS;
    __atomic_store_n(&c->entries[slot],entry0,__ATOMIC_RELAXED);
    __atomic_store_n(&c->entries[slot+1U],entry1,__ATOMIC_RELAXED);
    /* Publish both entry words before USERD GP_PUT and the host doorbell. */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_store_n(c->gp_put,next,__ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_store_n(c->doorbell,c->doorbell_token,__ATOMIC_RELEASE);
    c->put_index=next;
    *out_put=next;
    return NVX_GSP_GPFIFO_OK;
}

nvx_gsp_gpfifo_status_t nvx_gsp_gpfifo_submit_and_wait(
    nvx_gsp_gpfifo_channel_t *c,uint64_t command_va,size_t command_bytes,
    const volatile uint32_t *completion_word,uint32_t expected_completion,
    uint32_t timeout_ms,uint32_t interval_ms,
    nvx_gsp_gpfifo_delay_fn delay_ms,void *context,uint32_t *out_put)
{
    nvx_gsp_gpfifo_status_t s;
    uint32_t expected_get=0U;
    if(out_put)*out_put=0U;
    if(!c||!completion_word||!out_put||!timeout_ms||!interval_ms||!delay_ms)
        return NVX_GSP_GPFIFO_INVALID_ARGUMENT;
    s=nvx_gsp_gpfifo_submit(c,command_va,command_bytes,&expected_get);
    if(s!=NVX_GSP_GPFIFO_OK)return s;
    *out_put=expected_get;
    s=nvx_gsp_gpfifo_wait_get(c,expected_get,timeout_ms,interval_ms,
                              delay_ms,context);
    if(s!=NVX_GSP_GPFIFO_OK)return s;
    return nvx_gsp_gpfifo_wait_completion(completion_word,
        expected_completion,timeout_ms,interval_ms,delay_ms,context);
}

nvx_gsp_gpfifo_status_t nvx_gsp_gpfifo_wait_get(
    nvx_gsp_gpfifo_channel_t *c,uint32_t expected,uint32_t timeout_ms,
    uint32_t interval_ms,nvx_gsp_gpfifo_delay_fn delay_ms,void *context)
{
    uint32_t elapsed=0U;
    if(!c||!c->initialized||!c->gp_get||expected>=c->entry_count||
       !timeout_ms||!interval_ms||!delay_ms)
        return NVX_GSP_GPFIFO_INVALID_ARGUMENT;
    for(;;) {
        uint32_t get=__atomic_load_n(c->gp_get,__ATOMIC_ACQUIRE);
        if(get>=c->entry_count)return NVX_GSP_GPFIFO_BAD_REGISTER_STATE;
        if(get==expected)return NVX_GSP_GPFIFO_OK;
        if(elapsed>=timeout_ms)return NVX_GSP_GPFIFO_TIMEOUT;
        uint32_t wait=interval_ms;
        if(wait>timeout_ms-elapsed)wait=timeout_ms-elapsed;
        delay_ms(wait,context);
        elapsed+=wait;
    }
}

nvx_gsp_gpfifo_status_t nvx_gsp_gpfifo_wait_completion(
    const volatile uint32_t *word,uint32_t expected,uint32_t timeout_ms,
    uint32_t interval_ms,nvx_gsp_gpfifo_delay_fn delay_ms,void *context)
{
    uint32_t elapsed=0U;
    if(!word||((uintptr_t)word&3U)!=0U||!timeout_ms||!interval_ms||!delay_ms)
        return NVX_GSP_GPFIFO_INVALID_ARGUMENT;
    for(;;) {
        if(__atomic_load_n(word,__ATOMIC_ACQUIRE)==expected)
            return NVX_GSP_GPFIFO_OK;
        if(elapsed>=timeout_ms)return NVX_GSP_GPFIFO_TIMEOUT;
        uint32_t wait=interval_ms;
        if(wait>timeout_ms-elapsed)wait=timeout_ms-elapsed;
        delay_ms(wait,context);
        elapsed+=wait;
    }
}
