/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_rmapi.h"
#include "nvx_gsp_ring.h"

#include <string.h>
#include <stdint.h>
#include <limits.h>

#define ALLOC_HEADER_BYTES 32U
#define CONTROL_HEADER_BYTES 40U
#define FREE_HEADER_BYTES 16U

static void put32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8U);
    p[2]=(uint8_t)(v>>16U); p[3]=(uint8_t)(v>>24U);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8U) |
           ((uint32_t)p[2]<<16U) | ((uint32_t)p[3]<<24U);
}

static int add_size(size_t a, size_t b, size_t *out)
{
    if (a>SIZE_MAX-b) return 0;
    *out=a+b;
    return 1;
}

static int overlaps(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!an||!bn)return 0;
    if(!a||!b||x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn)return 1;
    return x<y+bn&&y<x+an;
}

static nvx_gsp_rmapi_status_t submit(
    nvx_gsp_rpc_manager_t *manager,uint32_t function,
    const void *request,size_t request_size,
    void *scratch,size_t scratch_size,void *response,size_t response_capacity,
    size_t minimum_response,nvx_gsp_rmapi_result_t *out)
{
    nvx_gsp_rpc_handle_t handle={0};
    nvx_gsp_rpc_status_t status;
    if(out) *out=(nvx_gsp_rmapi_result_t){0};
    if(!manager||!request||!request_size||!scratch||!out||
       !response||response_capacity<minimum_response||
       overlaps(out,sizeof(*out),request,request_size)||
       overlaps(out,sizeof(*out),response,response_capacity)||
       overlaps(scratch,scratch_size,response,response_capacity))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    status=nvx_gsp_rpc_submit(manager,NVX_GSP_RPC_HEADER_VERSION_R535,
        NVX_GSP_RPC_SIGNATURE_R535,function,function,request,request_size,
        scratch,scratch_size,response,response_capacity,&handle);
    if(status!=NVX_GSP_RPC_OK) return NVX_GSP_RMAPI_RPC_ERROR;
    out->handle=handle;
    out->function=function;
    return NVX_GSP_RMAPI_OK;
}

nvx_gsp_rmapi_status_t nvx_gsp_rm_alloc_submit(
    nvx_gsp_rpc_manager_t *manager,uint32_t client,uint32_t parent,
    uint32_t object,uint32_t class_id,uint32_t flags,
    const void *params,size_t params_size,void *scratch,size_t scratch_size,
    void *response,size_t response_capacity,nvx_gsp_rmapi_result_t *out)
{
    uint8_t request[ALLOC_HEADER_BYTES+NVX_GSP_RMAPI_MAX_PARAMS];
    size_t bytes;
    if((params_size&&!params)||params_size>NVX_GSP_RMAPI_MAX_PARAMS||
       !add_size(ALLOC_HEADER_BYTES,params_size,&bytes))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    memset(request,0,bytes);
    put32(request+0U,client);put32(request+4U,parent);
    put32(request+8U,object);put32(request+12U,class_id);
    put32(request+16U,0U);put32(request+20U,(uint32_t)params_size);
    put32(request+24U,flags);
    if(params_size)memcpy(request+ALLOC_HEADER_BYTES,params,params_size);
    return submit(manager,NVX_GSP_RPC_FN_RM_ALLOC,request,bytes,scratch,
        scratch_size,response,response_capacity,ALLOC_HEADER_BYTES,out);
}

nvx_gsp_rmapi_status_t nvx_gsp_rm_control_submit(
    nvx_gsp_rpc_manager_t *manager,uint32_t client,uint32_t object,
    uint32_t command,uint32_t rpc_flags,uint32_t ctrl_flags,
    uint32_t access_right,const void *params,size_t params_size,
    void *scratch,size_t scratch_size,void *response,size_t response_capacity,
    nvx_gsp_rmapi_result_t *out)
{
    uint8_t request[CONTROL_HEADER_BYTES+NVX_GSP_RMAPI_MAX_PARAMS];
    size_t bytes;
    if((params_size&&!params)||params_size>NVX_GSP_RMAPI_MAX_PARAMS||
       !add_size(CONTROL_HEADER_BYTES,params_size,&bytes))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    memset(request,0,bytes);
    put32(request+0U,client);put32(request+4U,object);
    put32(request+8U,command);put32(request+12U,0U);
    put32(request+16U,(uint32_t)params_size);put32(request+20U,rpc_flags);
    put32(request+24U,ctrl_flags);put32(request+28U,access_right);
    /* bytes [32,40) are the ABI's 64-bit reserved field and stay zero. */
    if(params_size)memcpy(request+CONTROL_HEADER_BYTES,params,params_size);
    return submit(manager,NVX_GSP_RPC_FN_RM_CONTROL,request,bytes,scratch,
        scratch_size,response,response_capacity,CONTROL_HEADER_BYTES,out);
}

nvx_gsp_rmapi_status_t nvx_gsp_rm_free_submit(
    nvx_gsp_rpc_manager_t *manager,uint32_t root,uint32_t parent,
    uint32_t object,void *scratch,size_t scratch_size,void *response,
    size_t response_capacity,nvx_gsp_rmapi_result_t *out)
{
    uint8_t request[FREE_HEADER_BYTES]={0};
    put32(request+0U,root);put32(request+4U,parent);
    put32(request+8U,object);put32(request+12U,0U);
    return submit(manager,NVX_GSP_RPC_FN_FREE,request,sizeof(request),scratch,
        scratch_size,response,response_capacity,FREE_HEADER_BYTES,out);
}

nvx_gsp_rmapi_status_t nvx_gsp_rmapi_wait(
    nvx_gsp_rpc_manager_t *manager,nvx_gsp_rmapi_result_t *operation,
    void *scratch,size_t scratch_size,void *response,size_t response_capacity,
    uint32_t timeout_ms,uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms,void *delay_context)
{
    uint32_t elapsed=0U;
    if(!manager||!operation||!operation->handle.valid||!scratch||!response||
       !timeout_ms||!poll_interval_ms||!delay_ms||
       overlaps(scratch,scratch_size,response,response_capacity))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    for(;;) {
        nvx_gsp_rpc_view_t unmatched={0};
        nvx_gsp_rpc_result_t result={0};
        nvx_gsp_rpc_status_t pumped=nvx_gsp_rpc_pump(
            manager,scratch,scratch_size,&unmatched);
        nvx_gsp_rpc_status_t queried=nvx_gsp_rpc_query(
            manager,&operation->handle,&result);
        if(queried==NVX_GSP_RPC_COMPLETE) {
            uint32_t remote_status=UINT32_MAX;
            operation->response_size=result.response_size;
            return nvx_gsp_rmapi_decode_status(operation,response,
                result.response_size,&remote_status);
        }
        if(queried==NVX_GSP_RPC_FAILED||queried==NVX_GSP_RPC_DEVICE_LOST)
            return NVX_GSP_RMAPI_RPC_ERROR;
        if(queried==NVX_GSP_RPC_FUNCTION_MISMATCH||
           queried==NVX_GSP_RPC_RESPONSE_TOO_LARGE||
           queried==NVX_GSP_RPC_STALE_HANDLE)
            return NVX_GSP_RMAPI_MALFORMED_RESPONSE;
        if(pumped==NVX_GSP_RPC_TRANSPORT_ERROR||
           pumped==NVX_GSP_RPC_NOT_INITIALIZED)
            return NVX_GSP_RMAPI_RPC_ERROR;
        if(elapsed>=timeout_ms)return NVX_GSP_RMAPI_TIMEOUT;
        uint32_t wait=poll_interval_ms;
        if(wait>timeout_ms-elapsed)wait=timeout_ms-elapsed;
        delay_ms(wait,delay_context);
        elapsed+=wait;
    }
}

static void put64(uint8_t *p,uint64_t v)
{
    put32(p,(uint32_t)v);put32(p+4U,(uint32_t)(v>>32U));
}

nvx_gsp_rmapi_status_t nvx_gsp_rm_map_memory_dma_submit(
    nvx_gsp_rpc_manager_t *manager,const nvx_gsp_map_memory_dma_t *m,
    void *scratch,size_t scratch_size,void *response,size_t response_capacity,
    nvx_gsp_rmapi_result_t *out)
{
    uint8_t request[64]={0};
    if(!m)return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    /* NVOS46_PARAMETERS: offsets follow the public 64-bit-aligned ABI. */
    put32(request+0U,m->client);put32(request+4U,m->device);
    put32(request+8U,m->dma);put32(request+12U,m->memory);
    put64(request+16U,m->offset);put64(request+24U,m->length);
    put32(request+32U,m->flags);put32(request+36U,m->flags2);
    put32(request+40U,m->kind_override);put64(request+48U,m->dma_offset);
    return submit(manager,NVX_GSP_RPC_FN_MAP_MEMORY_DMA,request,sizeof(request),
        scratch,scratch_size,response,response_capacity,60U,out);
}

nvx_gsp_rmapi_status_t nvx_gsp_rm_unmap_memory_dma_submit(
    nvx_gsp_rpc_manager_t *manager,const nvx_gsp_unmap_memory_dma_t *m,
    void *scratch,size_t scratch_size,void *response,size_t response_capacity,
    nvx_gsp_rmapi_result_t *out)
{
    uint8_t request[48]={0};
    if(!m)return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    /* NVOS47_PARAMETERS includes alignment before dmaOffset and size. */
    put32(request+0U,m->client);put32(request+4U,m->device);
    put32(request+8U,m->dma);put32(request+12U,m->memory);
    put32(request+16U,m->flags);put64(request+24U,m->dma_offset);
    put64(request+32U,m->size);
    return submit(manager,NVX_GSP_RPC_FN_UNMAP_MEMORY_DMA,request,sizeof(request),
        scratch,scratch_size,response,response_capacity,44U,out);
}

nvx_gsp_rmapi_status_t nvx_gsp_rm_map_memory_dma_result(
    const nvx_gsp_rmapi_result_t *operation,const void *response,
    size_t response_size,uint64_t *out_dma_offset)
{
    const uint8_t *p=(const uint8_t *)response;
    uint32_t status;
    nvx_gsp_rmapi_status_t decoded;
    if(out_dma_offset)*out_dma_offset=0U;
    if(!operation||!response||!out_dma_offset||
       operation->function!=NVX_GSP_RPC_FN_MAP_MEMORY_DMA)
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    if(response_size<60U)return NVX_GSP_RMAPI_RESPONSE_TOO_SMALL;
    decoded=nvx_gsp_rmapi_decode_status(operation,response,response_size,&status);
    if(decoded!=NVX_GSP_RMAPI_OK)return decoded;
    *out_dma_offset=(uint64_t)get32(p+48U)|((uint64_t)get32(p+52U)<<32U);
    return NVX_GSP_RMAPI_OK;
}

static nvx_gsp_rmapi_status_t free_core_object(
    nvx_gsp_rpc_manager_t *manager,uint32_t root,uint32_t parent,
    uint32_t object,void *scratch,size_t scratch_size,void *response,
    size_t response_capacity,uint32_t timeout_ms,uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms,void *delay_context)
{
    nvx_gsp_rmapi_result_t operation={0};
    return nvx_gsp_rm_free_sync(manager,root,parent,object,scratch,scratch_size,
        response,response_capacity,timeout_ms,poll_interval_ms,delay_ms,
        delay_context,&operation);
}

nvx_gsp_rmapi_status_t nvx_gsp_rm_create_core_objects(
    nvx_gsp_rpc_manager_t *manager,uint32_t client,uint32_t device,
    uint32_t vaspace,uint32_t device_flags,uint64_t device_va_size,
    uint64_t vaspace_limit,void *scratch,size_t scratch_size,void *response,
    size_t response_capacity,uint32_t timeout_ms,uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms,void *delay_context,
    nvx_gsp_rm_core_objects_t *out)
{
    nvx_gsp_rmapi_result_t operation={0};
    nvx_gsp_rmapi_status_t s;
    uint8_t params[120]={0};
    if(out)*out=(nvx_gsp_rm_core_objects_t){0};
    if(!manager||!client||!device||!vaspace||client==device||
       client==vaspace||device==vaspace||!scratch||!response||
       response_capacity<ALLOC_HEADER_BYTES||!timeout_ms||!poll_interval_ms||
       !delay_ms||!out||
       overlaps(out,sizeof(*out),scratch,scratch_size)||
       overlaps(out,sizeof(*out),response,response_capacity))
        return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    out->client_handle=client;out->device_handle=device;out->vaspace_handle=vaspace;

    /*
     * NV0000_ALLOC_PARAMETERS from the public R535 SDK: hClient, processID,
     * 100-byte processName, and aligned pOsPidInfo. The pointer stays NULL.
     */
    put32(params,client);
    memcpy(params+8U,"VantaOS",7U);
    out->in_flight_stage=1U;
    s=nvx_gsp_rm_alloc_sync(manager,0U,0U,client,NVX_GSP_RM_CLASS_ROOT,0U,
        params,120U,scratch,scratch_size,response,response_capacity,timeout_ms,
        poll_interval_ms,delay_ms,delay_context,&operation);
    if(s!=NVX_GSP_RMAPI_OK) { if(s!=NVX_GSP_RMAPI_TIMEOUT)out->in_flight_stage=0U; return s; }
    out->client_created=1U;out->in_flight_stage=0U;

    /*
     * NV0080_ALLOC_PARAMETERS is 56 bytes in the public R535 ABI:
     * deviceId, hClientShare, hTargetClient, hTargetDevice, flags,
     * vaSpaceSize, vaStartInternal, vaLimitInternal, vaMode.
     */
    memset(params,0,sizeof(params));
    put32(params+16U,device_flags);
    put64(params+24U,device_va_size);
    out->in_flight_stage=2U;
    s=nvx_gsp_rm_alloc_sync(manager,client,client,device,
        NVX_GSP_RM_CLASS_DEVICE_0,0U,params,56U,scratch,scratch_size,
        response,response_capacity,timeout_ms,poll_interval_ms,delay_ms,
        delay_context,&operation);
    if(s!=NVX_GSP_RMAPI_OK) {
        if(s==NVX_GSP_RMAPI_TIMEOUT)return s;
        out->in_flight_stage=0U;
        if(free_core_object(manager,client,0U,client,
           scratch,scratch_size,response,response_capacity,timeout_ms,
           poll_interval_ms,delay_ms,delay_context)==NVX_GSP_RMAPI_OK)
            out->client_created=0U;
        return s;
    }
    out->device_created=1U;out->in_flight_stage=0U;

    /*
     * NV_MEMORY_VIRTUAL_ALLOCATION_PARAMS is 24 bytes: offset, limit, hVASpace.
     * Use offset 0 and NV01_NULL_OBJECT as the default parent address space.
     */
    memset(params,0,sizeof(params));
    put64(params+8U,vaspace_limit);
    put32(params+16U,0U);
    out->in_flight_stage=3U;
    s=nvx_gsp_rm_alloc_sync(manager,client,device,vaspace,
        NVX_GSP_RM_CLASS_MEMORY_VIRTUAL,0U,params,24U,scratch,
        scratch_size,response,response_capacity,timeout_ms,poll_interval_ms,
        delay_ms,delay_context,&operation);
    if(s!=NVX_GSP_RMAPI_OK) {
        if(s==NVX_GSP_RMAPI_TIMEOUT)return s;
        out->in_flight_stage=0U;
        if(free_core_object(manager,client,client,
           device,scratch,scratch_size,response,response_capacity,timeout_ms,
           poll_interval_ms,delay_ms,delay_context)==NVX_GSP_RMAPI_OK)
            out->device_created=0U;
        if(s!=NVX_GSP_RMAPI_TIMEOUT&&free_core_object(manager,client,0U,client,
           scratch,scratch_size,response,response_capacity,timeout_ms,
           poll_interval_ms,delay_ms,delay_context)==NVX_GSP_RMAPI_OK)
            out->client_created=0U;
        return s;
    }
    out->vaspace_created=1U;out->in_flight_stage=0U;
    return NVX_GSP_RMAPI_OK;
}

static nvx_gsp_rmapi_status_t finish_sync(
    nvx_gsp_rpc_manager_t *manager,nvx_gsp_rmapi_result_t *out,
    void *scratch,size_t scratch_size,void *response,size_t response_capacity,
    uint32_t timeout_ms,uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms,void *delay_context)
{
    nvx_gsp_rmapi_status_t status=nvx_gsp_rmapi_wait(manager,out,scratch,
        scratch_size,response,response_capacity,timeout_ms,poll_interval_ms,
        delay_ms,delay_context);
    /*
     * Release succeeds only after completion; on timeout it is deliberately
     * not attempted because an in-flight reply can still arrive later.
     */
    if(status!=NVX_GSP_RMAPI_TIMEOUT&&out->handle.valid)
        (void)nvx_gsp_rpc_release(manager,&out->handle);
    return status;
}

nvx_gsp_rmapi_status_t nvx_gsp_rm_alloc_sync(
    nvx_gsp_rpc_manager_t *manager,uint32_t client,uint32_t parent,
    uint32_t object,uint32_t class_id,uint32_t flags,const void *params,
    size_t params_size,void *scratch,size_t scratch_size,void *response,
    size_t response_capacity,uint32_t timeout_ms,uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms,void *delay_context,
    nvx_gsp_rmapi_result_t *out)
{
    nvx_gsp_rmapi_status_t s=nvx_gsp_rm_alloc_submit(manager,client,parent,
        object,class_id,flags,params,params_size,scratch,scratch_size,
        response,response_capacity,out);
    return s==NVX_GSP_RMAPI_OK?finish_sync(manager,out,scratch,scratch_size,
        response,response_capacity,timeout_ms,poll_interval_ms,delay_ms,
        delay_context):s;
}

nvx_gsp_rmapi_status_t nvx_gsp_rm_control_sync(
    nvx_gsp_rpc_manager_t *manager,uint32_t client,uint32_t object,
    uint32_t command,uint32_t rpc_flags,uint32_t ctrl_flags,
    uint32_t access_right,const void *params,size_t params_size,
    void *scratch,size_t scratch_size,void *response,size_t response_capacity,
    uint32_t timeout_ms,uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms,void *delay_context,
    nvx_gsp_rmapi_result_t *out)
{
    nvx_gsp_rmapi_status_t s=nvx_gsp_rm_control_submit(manager,client,object,
        command,rpc_flags,ctrl_flags,access_right,params,params_size,scratch,
        scratch_size,response,response_capacity,out);
    return s==NVX_GSP_RMAPI_OK?finish_sync(manager,out,scratch,scratch_size,
        response,response_capacity,timeout_ms,poll_interval_ms,delay_ms,
        delay_context):s;
}

nvx_gsp_rmapi_status_t nvx_gsp_rmapi_schedule_channel_sync(
    nvx_gsp_rpc_manager_t *manager,uint32_t client,uint32_t channel,int enable,
    void *scratch,size_t scratch_size,void *response,size_t response_capacity,
    uint32_t timeout_ms,uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms,void *delay_context,
    nvx_gsp_rmapi_result_t *out)
{
    uint8_t params[12]={0};
    if(enable!=0&&enable!=1)return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    /* NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS: bEnable, bSkipSubmit, bSkipEnable. */
    put32(params,enable?1U:0U);
    put32(params+4U,0U);
    put32(params+8U,0U);
    return nvx_gsp_rm_control_sync(manager,client,channel,
        NVX_GSP_RM_CTRL_GPFIFO_SCHEDULE,0U,0U,0U,params,sizeof(params),
        scratch,scratch_size,response,response_capacity,timeout_ms,
        poll_interval_ms,delay_ms,delay_context,out);
}

nvx_gsp_rmapi_status_t nvx_gsp_rm_free_sync(
    nvx_gsp_rpc_manager_t *manager,uint32_t root,uint32_t parent,
    uint32_t object,void *scratch,size_t scratch_size,void *response,
    size_t response_capacity,uint32_t timeout_ms,uint32_t poll_interval_ms,
    nvx_gsp_rmapi_delay_fn delay_ms,void *delay_context,
    nvx_gsp_rmapi_result_t *out)
{
    nvx_gsp_rmapi_status_t s=nvx_gsp_rm_free_submit(manager,root,parent,
        object,scratch,scratch_size,response,response_capacity,out);
    return s==NVX_GSP_RMAPI_OK?finish_sync(manager,out,scratch,scratch_size,
        response,response_capacity,timeout_ms,poll_interval_ms,delay_ms,
        delay_context):s;
}

nvx_gsp_rmapi_status_t nvx_gsp_rmapi_decode_status(
    const nvx_gsp_rmapi_result_t *op,const void *response,size_t response_size,
    uint32_t *out_status)
{
    const uint8_t *p=(const uint8_t *)response;
    size_t offset;
    if(out_status)*out_status=UINT32_MAX;
    if(!op||!response||!out_status) return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    if(op->function==NVX_GSP_RPC_FN_RM_ALLOC) {
        if(response_size<ALLOC_HEADER_BYTES)return NVX_GSP_RMAPI_RESPONSE_TOO_SMALL;
        offset=16U;
    } else if(op->function==NVX_GSP_RPC_FN_RM_CONTROL) {
        if(response_size<CONTROL_HEADER_BYTES)return NVX_GSP_RMAPI_RESPONSE_TOO_SMALL;
        offset=12U;
    } else if(op->function==NVX_GSP_RPC_FN_FREE) {
        if(response_size<FREE_HEADER_BYTES)return NVX_GSP_RMAPI_RESPONSE_TOO_SMALL;
        offset=12U;
    } else if(op->function==NVX_GSP_RPC_FN_MAP_MEMORY_DMA) {
        if(response_size<60U)return NVX_GSP_RMAPI_RESPONSE_TOO_SMALL;
        offset=56U;
    } else if(op->function==NVX_GSP_RPC_FN_UNMAP_MEMORY_DMA) {
        if(response_size<44U)return NVX_GSP_RMAPI_RESPONSE_TOO_SMALL;
        offset=40U;
    } else return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    *out_status=get32(p+offset);
    return *out_status==0U?NVX_GSP_RMAPI_OK:NVX_GSP_RMAPI_REMOTE_ERROR;
}
