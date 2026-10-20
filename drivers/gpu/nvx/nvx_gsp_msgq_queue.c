/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_msgq_queue.h"
#include <string.h>
static int mmio_read32(void *context,uint32_t reg,uint32_t *out)
{
    nvx_gsp_msgq_mmio_context_t *m=(nvx_gsp_msgq_mmio_context_t *)context;
    return m&&m->region&&nvx_mmio_read32(m->region,reg,out)==NVX_MMIO_OK;
}
static int mmio_write32(void *context,uint32_t reg,uint32_t value)
{
    nvx_gsp_msgq_mmio_context_t *m=(nvx_gsp_msgq_mmio_context_t *)context;
    return m&&m->region&&nvx_mmio_write32(m->region,reg,value)==NVX_MMIO_OK;
}
static void mmio_before_device(void *context)
{
    (void)context;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __asm__ volatile("mfence" ::: "memory");
}
static void mmio_after_device(void *context)
{
    (void)context;
    __asm__ volatile("mfence" ::: "memory");
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}
nvx_gsp_msgq_queue_status_t nvx_gsp_msgq_io_init_mmio(
    nvx_gsp_msgq_io_t *out,nvx_gsp_msgq_mmio_context_t *context,
    const nvx_mmio_region_t *region)
{
    if(!out||!context||!region||!region->base||!region->length)
        return NVX_GSP_MSGQ_QUEUE_INVALID_ARGUMENT;
    context->region=region;
    *out=(nvx_gsp_msgq_io_t){mmio_read32,mmio_write32,
        mmio_before_device,mmio_after_device,context};
    return NVX_GSP_MSGQ_QUEUE_OK;
}
static int io_valid(const nvx_gsp_msgq_io_t *io)
{
    return io&&io->read32&&io->write32&&io->before_device&&io->after_device;
}
static nvx_gsp_msgq_queue_status_t init_common(nvx_gsp_msgq_queue_t *q,
    void *store,size_t bytes,size_t elem,const nvx_gsp_msgq_io_t *io,
    uint32_t head,uint32_t tail,int transmit)
{
    nvx_gsp_msgq_status_t status;
    if(!q||!store||!io_valid(io)||head==tail||(head&3U)||(tail&3U))
        return NVX_GSP_MSGQ_QUEUE_INVALID_ARGUMENT;
    if(q->initialized) return NVX_GSP_MSGQ_QUEUE_INVALID_ARGUMENT;
    memset(q,0,sizeof(*q));
    q->backing_store=(uint8_t *)store;q->io=*io;
    q->head_register=head;q->tail_register=tail;q->transmit=(uint8_t)(transmit!=0);
    if(transmit) status=nvx_gsp_msgq_prepare_tx(store,bytes,elem,&q->layout);
    else {
        q->io.after_device(q->io.context);
        status=nvx_gsp_msgq_validate_rx(store,bytes,elem,&q->layout);
    }
    if(status!=NVX_GSP_MSGQ_OK) { memset(q,0,sizeof(*q)); return NVX_GSP_MSGQ_QUEUE_BAD_LAYOUT; }
    q->local_counter=0U;q->peer_counter=0U;
    q->io.before_device(q->io.context);
    if(transmit) {
        if(!q->io.write32(q->io.context,head,0U)) {
            memset(q,0,sizeof(*q));return NVX_GSP_MSGQ_QUEUE_IO_ERROR;
        }
    } else if(!q->io.write32(q->io.context,tail,0U)) {
        memset(q,0,sizeof(*q));return NVX_GSP_MSGQ_QUEUE_IO_ERROR;
    }
    q->initialized=1U;
    return NVX_GSP_MSGQ_QUEUE_OK;
}
nvx_gsp_msgq_queue_status_t nvx_gsp_msgq_queue_init_tx(nvx_gsp_msgq_queue_t *q,
    void *store,size_t bytes,size_t elem,const nvx_gsp_msgq_io_t *io,uint32_t head,uint32_t tail)
{
    return init_common(q,store,bytes,elem,io,head,tail,1);
}
nvx_gsp_msgq_queue_status_t nvx_gsp_msgq_queue_init_rx(nvx_gsp_msgq_queue_t *q,
    void *store,size_t bytes,size_t elem,const nvx_gsp_msgq_io_t *io,uint32_t head,uint32_t tail)
{
    return init_common(q,store,bytes,elem,io,head,tail,0);
}
static nvx_gsp_msgq_queue_status_t update_peer(nvx_gsp_msgq_queue_t *q)
{
    uint32_t v;
    if(!q||!q->initialized) return NVX_GSP_MSGQ_QUEUE_NOT_INITIALIZED;
    if(!q->io.read32(q->io.context,q->transmit?q->tail_register:q->head_register,&v))
        return NVX_GSP_MSGQ_QUEUE_IO_ERROR;
    q->io.after_device(q->io.context);
    q->peer_counter=v;
    uint32_t distance=q->transmit?
        (uint32_t)(q->local_counter-q->peer_counter):
        (uint32_t)(q->peer_counter-q->local_counter);
    if(distance>q->layout.element_count)
        return NVX_GSP_MSGQ_QUEUE_BAD_COUNTER;
    return NVX_GSP_MSGQ_QUEUE_OK;
}
nvx_gsp_msgq_queue_status_t nvx_gsp_msgq_tx_get(nvx_gsp_msgq_queue_t *q,
    uint32_t index,uint8_t **out)
{
    nvx_gsp_msgq_queue_status_t s;uint32_t used,free_count,slot;
    if(out) *out=0;
    if(!q||!out) return NVX_GSP_MSGQ_QUEUE_INVALID_ARGUMENT;
    if(!q->initialized) return NVX_GSP_MSGQ_QUEUE_NOT_INITIALIZED;
    if(!q->transmit) return NVX_GSP_MSGQ_QUEUE_INVALID_ARGUMENT;
    s=update_peer(q);if(s!=NVX_GSP_MSGQ_QUEUE_OK)return s;
    used=(uint32_t)(q->local_counter-q->peer_counter);
    free_count=q->layout.element_count-used;
    if(index>=free_count)return NVX_GSP_MSGQ_QUEUE_FULL;
    slot=(q->local_counter+index)%q->layout.element_count;
    *out=q->layout.entries+(size_t)slot*q->layout.element_size;
    return NVX_GSP_MSGQ_QUEUE_OK;
}
nvx_gsp_msgq_queue_status_t nvx_gsp_msgq_tx_submit(nvx_gsp_msgq_queue_t *q,uint32_t count)
{
    nvx_gsp_msgq_queue_status_t s;uint32_t next,used;
    if(!q) return NVX_GSP_MSGQ_QUEUE_INVALID_ARGUMENT;
    if(!q->initialized) return NVX_GSP_MSGQ_QUEUE_NOT_INITIALIZED;
    if(!q->transmit||!count||count>q->layout.element_count)
        return NVX_GSP_MSGQ_QUEUE_INVALID_ARGUMENT;
    s=update_peer(q);if(s!=NVX_GSP_MSGQ_QUEUE_OK)return s;
    used=(uint32_t)(q->local_counter-q->peer_counter);
    if(count>q->layout.element_count-used)return NVX_GSP_MSGQ_QUEUE_FULL;
    next=q->local_counter+count;
    q->io.before_device(q->io.context);
    if(!q->io.write32(q->io.context,q->head_register,next))
        return NVX_GSP_MSGQ_QUEUE_IO_ERROR;
    q->local_counter=next;
    return NVX_GSP_MSGQ_QUEUE_OK;
}
nvx_gsp_msgq_queue_status_t nvx_gsp_msgq_rx_get(nvx_gsp_msgq_queue_t *q,
    uint32_t index,const uint8_t **out)
{
    nvx_gsp_msgq_queue_status_t s;uint32_t available,slot;
    if(out) *out=0;
    if(!q||!out) return NVX_GSP_MSGQ_QUEUE_INVALID_ARGUMENT;
    if(!q->initialized) return NVX_GSP_MSGQ_QUEUE_NOT_INITIALIZED;
    if(q->transmit) return NVX_GSP_MSGQ_QUEUE_INVALID_ARGUMENT;
    s=update_peer(q);if(s!=NVX_GSP_MSGQ_QUEUE_OK)return s;
    q->io.after_device(q->io.context);
    available=(uint32_t)(q->peer_counter-q->local_counter);
    if(available>q->layout.element_count)return NVX_GSP_MSGQ_QUEUE_BAD_COUNTER;
    if(index>=available)return NVX_GSP_MSGQ_QUEUE_EMPTY;
    slot=(q->local_counter+index)%q->layout.element_count;
    *out=q->layout.entries+(size_t)slot*q->layout.element_size;
    return NVX_GSP_MSGQ_QUEUE_OK;
}
nvx_gsp_msgq_queue_status_t nvx_gsp_msgq_rx_consume(nvx_gsp_msgq_queue_t *q,uint32_t count)
{
    nvx_gsp_msgq_queue_status_t s;uint32_t available,next;
    if(!q) return NVX_GSP_MSGQ_QUEUE_INVALID_ARGUMENT;
    if(!q->initialized) return NVX_GSP_MSGQ_QUEUE_NOT_INITIALIZED;
    if(q->transmit||!count||count>q->layout.element_count)
        return NVX_GSP_MSGQ_QUEUE_INVALID_ARGUMENT;
    s=update_peer(q);if(s!=NVX_GSP_MSGQ_QUEUE_OK)return s;
    available=(uint32_t)(q->peer_counter-q->local_counter);
    if(available>q->layout.element_count)return NVX_GSP_MSGQ_QUEUE_BAD_COUNTER;
    if(count>available)return NVX_GSP_MSGQ_QUEUE_EMPTY;
    next=q->local_counter+count;
    q->io.before_device(q->io.context);
    if(!q->io.write32(q->io.context,q->tail_register,next))
        return NVX_GSP_MSGQ_QUEUE_IO_ERROR;
    q->local_counter=next;
    return NVX_GSP_MSGQ_QUEUE_OK;
}
