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
    put32(request+16U,UINT32_MAX);put32(request+20U,(uint32_t)params_size);
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
    put32(request+8U,command);put32(request+12U,UINT32_MAX);
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
    put32(request+8U,object);put32(request+12U,UINT32_MAX);
    return submit(manager,NVX_GSP_RPC_FN_FREE,request,sizeof(request),scratch,
        scratch_size,response,response_capacity,FREE_HEADER_BYTES,out);
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
    } else return NVX_GSP_RMAPI_INVALID_ARGUMENT;
    *out_status=get32(p+offset);
    return *out_status==0U?NVX_GSP_RMAPI_OK:NVX_GSP_RMAPI_REMOTE_ERROR;
}
