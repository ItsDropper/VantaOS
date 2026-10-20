/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_rpc.h"
#include <string.h>
static uint64_t rpc_lock(nvx_gsp_rpc_manager_t *m) {
#ifdef NVX_GSP_RPC_TEST
    spinlock_lock(&m->lock); return 0U;
#else
    return spinlock_lock_irqsave(&m->lock);
#endif
}
static void rpc_unlock(nvx_gsp_rpc_manager_t *m,uint64_t flags) {
#ifdef NVX_GSP_RPC_TEST
    (void)flags; spinlock_unlock(&m->lock);
#else
    spinlock_unlock_irqrestore(&m->lock,flags);
#endif
}
static nvx_gsp_rpc_slot_t *find_handle(nvx_gsp_rpc_manager_t *m,const nvx_gsp_rpc_handle_t *h) {
    if(!h||!h->valid||h->slot>=NVX_GSP_RPC_MAX_PENDING) return 0;
    nvx_gsp_rpc_slot_t *s=&m->slots[h->slot];
    return s->active&&s->generation==h->generation?s:0;
}
nvx_gsp_rpc_status_t nvx_gsp_rpc_init(nvx_gsp_rpc_manager_t *m,nvx_gsp_ring_t *command_ring,nvx_gsp_ring_t *response_ring) {
    if(!m||!command_ring||!response_ring) return NVX_GSP_RPC_INVALID_ARGUMENT;
    if(m->initialized) return NVX_GSP_RPC_INVALID_ARGUMENT;
    if(!command_ring->initialized||!response_ring->initialized) return NVX_GSP_RPC_NOT_INITIALIZED;
    memset(m,0,sizeof(*m)); spinlock_init(&m->lock); m->command_ring=command_ring; m->response_ring=response_ring; m->initialized=1U;
    return NVX_GSP_RPC_OK;
}
nvx_gsp_rpc_status_t nvx_gsp_rpc_submit(nvx_gsp_rpc_manager_t *m,
    uint32_t version,uint32_t signature,uint32_t request_function,
    uint32_t response_function,const void *payload,size_t payload_size,
    void *scratch,size_t scratch_size,void *response,size_t response_capacity,
    nvx_gsp_rpc_handle_t *out_handle)
{
    size_t index=NVX_GSP_RPC_MAX_PENDING; nvx_gsp_rpc_slot_t *slot;
    uint64_t flags; uint32_t generation; nvx_gsp_ring_status_t sent;
    if(out_handle) *out_handle=(nvx_gsp_rpc_handle_t){0};
    if(!m||!out_handle||!scratch||(payload_size&&!payload)||
       (response_capacity&&!response)) return NVX_GSP_RPC_INVALID_ARGUMENT;
    if(!m->initialized) return NVX_GSP_RPC_NOT_INITIALIZED;
    flags=rpc_lock(m);
    if(m->device_lost) { rpc_unlock(m,flags); return NVX_GSP_RPC_DEVICE_LOST; }
    for(size_t i=0;i<NVX_GSP_RPC_MAX_PENDING;i++)
        if(!m->slots[i].active) { index=i; break; }
    if(index==NVX_GSP_RPC_MAX_PENDING) { rpc_unlock(m,flags); return NVX_GSP_RPC_NO_SLOTS; }
    slot=&m->slots[index]; generation=slot->generation+1U;
    if(generation==0U) generation=1U;
    memset(slot,0,sizeof(*slot)); slot->generation=generation;
    slot->request_function=request_function; slot->response_function=response_function;
    slot->response_buffer=response; slot->response_capacity=response_capacity;
    slot->state=NVX_GSP_RPC_PENDING; slot->active=1U;
    out_handle->slot=(uint16_t)index; out_handle->generation=generation; out_handle->valid=1U;
    rpc_unlock(m,flags);

    /* ring_send writes slot->sequence before publishing the queue write pointer. */
    sent=nvx_gsp_ring_send(m->command_ring,version,signature,request_function,payload,
                           payload_size,scratch,scratch_size,&slot->sequence);
    if(sent!=NVX_GSP_RING_OK) {
        flags=rpc_lock(m);
        if(slot->active&&slot->generation==generation) {
            memset(slot,0,sizeof(*slot)); slot->generation=generation;
        }
        rpc_unlock(m,flags); *out_handle=(nvx_gsp_rpc_handle_t){0};
        return sent==NVX_GSP_RING_NOT_INITIALIZED?NVX_GSP_RPC_NOT_INITIALIZED:NVX_GSP_RPC_SEND_FAILED;
    }
    return NVX_GSP_RPC_OK;
}
nvx_gsp_rpc_status_t nvx_gsp_rpc_pump(nvx_gsp_rpc_manager_t *m,void *scratch,
    size_t scratch_size,nvx_gsp_rpc_view_t *unmatched)
{
    nvx_gsp_rpc_view_t view; nvx_gsp_ring_status_t received;
    uint64_t flags; nvx_gsp_rpc_slot_t *slot=0;
    if(unmatched) memset(unmatched,0,sizeof(*unmatched));
    if(!m||!scratch) return NVX_GSP_RPC_INVALID_ARGUMENT;
    if(!m->initialized) return NVX_GSP_RPC_NOT_INITIALIZED;
    received=nvx_gsp_ring_receive(m->response_ring,scratch,scratch_size,&view);
    if(received==NVX_GSP_RING_QUEUE_EMPTY) return NVX_GSP_RPC_PENDING;
    if(received!=NVX_GSP_RING_OK) return NVX_GSP_RPC_TRANSPORT_ERROR;
    flags=rpc_lock(m);
    for(size_t i=0;i<NVX_GSP_RPC_MAX_PENDING;i++) {
        nvx_gsp_rpc_slot_t *candidate=&m->slots[i];
        if(candidate->active&&candidate->state==NVX_GSP_RPC_PENDING&&
           candidate->sequence!=0U&&candidate->sequence==view.sequence) {
            slot=candidate; break;
        }
    }
    if(!slot) {
        if(unmatched) *unmatched=view;
        rpc_unlock(m,flags); return NVX_GSP_RPC_UNMATCHED_MESSAGE;
    }
    slot->firmware_result=view.result; slot->firmware_result_private=view.result_private;
    slot->response_size=view.payload_size;
    if(view.function!=slot->response_function) {
        slot->state=NVX_GSP_RPC_FUNCTION_MISMATCH;
    } else if(view.payload_size>slot->response_capacity||
              (view.payload_size&&!slot->response_buffer)) {
        slot->state=NVX_GSP_RPC_RESPONSE_TOO_LARGE;
    } else {
        if(view.payload_size) memcpy(slot->response_buffer,view.payload,view.payload_size);
        slot->state=NVX_GSP_RPC_COMPLETE;
    }
    nvx_gsp_rpc_status_t result=slot->state;
    rpc_unlock(m,flags); return result;
}
nvx_gsp_rpc_status_t nvx_gsp_rpc_query(nvx_gsp_rpc_manager_t *m,
    const nvx_gsp_rpc_handle_t *h,nvx_gsp_rpc_result_t *out)
{
    uint64_t flags; nvx_gsp_rpc_slot_t *s;
    if(out) *out=(nvx_gsp_rpc_result_t){0};
    if(!m||!h||!out) return NVX_GSP_RPC_INVALID_ARGUMENT;
    if(!m->initialized) return NVX_GSP_RPC_NOT_INITIALIZED;
    flags=rpc_lock(m); s=find_handle(m,h);
    if(!s) { rpc_unlock(m,flags); return NVX_GSP_RPC_STALE_HANDLE; }
    out->sequence=s->sequence; out->function=s->request_function;
    out->firmware_result=s->firmware_result; out->firmware_result_private=s->firmware_result_private;
    out->response_size=s->response_size; out->status=s->state;
    nvx_gsp_rpc_status_t status=s->state; rpc_unlock(m,flags); return status;
}
nvx_gsp_rpc_status_t nvx_gsp_rpc_release(nvx_gsp_rpc_manager_t *m,nvx_gsp_rpc_handle_t *h)
{
    uint64_t flags; nvx_gsp_rpc_slot_t *s;
    if(!m||!h) return NVX_GSP_RPC_INVALID_ARGUMENT;
    if(!m->initialized) return NVX_GSP_RPC_NOT_INITIALIZED;
    flags=rpc_lock(m); s=find_handle(m,h);
    if(!s) { rpc_unlock(m,flags); return NVX_GSP_RPC_STALE_HANDLE; }
    if(s->state==NVX_GSP_RPC_PENDING) { rpc_unlock(m,flags); return NVX_GSP_RPC_BUSY; }
    uint32_t generation=s->generation; memset(s,0,sizeof(*s)); s->generation=generation;
    *h=(nvx_gsp_rpc_handle_t){0}; rpc_unlock(m,flags); return NVX_GSP_RPC_OK;
}
nvx_gsp_rpc_status_t nvx_gsp_rpc_mark_device_lost(nvx_gsp_rpc_manager_t *m)
{
    if(!m) return NVX_GSP_RPC_INVALID_ARGUMENT;
    if(!m->initialized) return NVX_GSP_RPC_NOT_INITIALIZED;
    uint64_t flags=rpc_lock(m); m->device_lost=1U;
    for(size_t i=0;i<NVX_GSP_RPC_MAX_PENDING;i++)
        if(m->slots[i].active&&m->slots[i].state==NVX_GSP_RPC_PENDING)
            m->slots[i].state=NVX_GSP_RPC_DEVICE_LOST;
    rpc_unlock(m,flags); return NVX_GSP_RPC_OK;
}
