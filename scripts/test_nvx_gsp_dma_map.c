/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_dma_map.h"

#include <assert.h>
#include <string.h>

static nvx_gsp_rmapi_status_t map_submit_status, unmap_submit_status,
    wait_status, result_status;
static nvx_gsp_rpc_status_t release_status;
static nvx_gsp_map_memory_dma_t last_map;
static nvx_gsp_unmap_memory_dma_t last_unmap;
static unsigned map_calls, unmap_calls, wait_calls, release_calls;
static uint64_t mapped_offset = 0x12345000ULL;

nvx_gsp_rmapi_status_t nvx_gsp_rm_map_memory_dma_submit(
    nvx_gsp_rpc_manager_t *m,const nvx_gsp_map_memory_dma_t *request,
    void *s,size_t ss,void *r,size_t rs,nvx_gsp_rmapi_result_t *out)
{
    (void)m;(void)s;(void)ss;(void)r;(void)rs;
    last_map=*request;map_calls++;
    out->function=NVX_GSP_RPC_FN_MAP_MEMORY_DMA;
    out->response_size=60U;out->handle.valid=1U;
    return map_submit_status;
}
nvx_gsp_rmapi_status_t nvx_gsp_rm_unmap_memory_dma_submit(
    nvx_gsp_rpc_manager_t *m,const nvx_gsp_unmap_memory_dma_t *request,
    void *s,size_t ss,void *r,size_t rs,nvx_gsp_rmapi_result_t *out)
{
    (void)m;(void)s;(void)ss;(void)r;(void)rs;
    last_unmap=*request;unmap_calls++;
    out->function=NVX_GSP_RPC_FN_UNMAP_MEMORY_DMA;
    out->response_size=44U;out->handle.valid=1U;
    return unmap_submit_status;
}
nvx_gsp_rmapi_status_t nvx_gsp_rmapi_wait(
    nvx_gsp_rpc_manager_t *m,nvx_gsp_rmapi_result_t *op,void *s,size_t ss,
    void *r,size_t rs,uint32_t timeout,uint32_t interval,
    nvx_gsp_rmapi_delay_fn delay,void *ctx)
{
    (void)m;(void)op;(void)s;(void)ss;(void)r;(void)rs;(void)timeout;
    (void)interval;(void)delay;(void)ctx;wait_calls++;return wait_status;
}
nvx_gsp_rmapi_status_t nvx_gsp_rm_map_memory_dma_result(
    const nvx_gsp_rmapi_result_t *op,const void *r,size_t rs,uint64_t *out)
{
    (void)op;(void)r;(void)rs;
    if(result_status==NVX_GSP_RMAPI_OK)*out=mapped_offset;
    return result_status;
}
nvx_gsp_rpc_status_t nvx_gsp_rpc_release(nvx_gsp_rpc_manager_t *m,
    nvx_gsp_rpc_handle_t *h)
{
    (void)m;release_calls++;
    if(release_status==NVX_GSP_RPC_OK&&h)*h=(nvx_gsp_rpc_handle_t){0};
    return release_status;
}
static void delay(uint32_t ms,void *ctx){(void)ms;(void)ctx;}

int main(void)
{
    nvx_gsp_rpc_manager_t manager={0};
    nvx_gsp_dma_mapping_t mapping={0};
    nvx_gsp_map_memory_dma_t request={.client=1U,.device=2U,.dma=3U,
        .memory=4U,.offset=0x2000U,.length=0x6000U,.flags=0x10U,
        .flags2=0x20U,.kind_override=7U,.dma_offset=UINT64_MAX};
    uint8_t scratch[128],response[128];

    map_submit_status=unmap_submit_status=wait_status=result_status=
        NVX_GSP_RMAPI_OK;
    release_status=NVX_GSP_RPC_OK;

    assert(nvx_gsp_dma_map(&manager,&mapping,&request,scratch,sizeof(scratch),
        response,sizeof(response),100U,1U,delay,NULL)==NVX_GSP_RMAPI_OK);
    assert(mapping.stage==NVX_GSP_DMA_MAP_MAPPED);
    assert(mapping.dma_offset==mapped_offset);
    assert(last_map.dma_offset==0U&&last_map.offset==request.offset&&
           last_map.length==request.length&&last_map.flags2==request.flags2);
    assert(release_calls==1U&&map_calls==1U);

    assert(nvx_gsp_dma_unmap(&manager,&mapping,scratch,sizeof(scratch),
        response,sizeof(response),100U,1U,delay,NULL)==NVX_GSP_RMAPI_OK);
    assert(mapping.stage==NVX_GSP_DMA_MAP_IDLE&&mapping.dma_offset==0U);
    assert(last_unmap.client==request.client&&last_unmap.device==request.device&&
           last_unmap.dma==request.dma&&last_unmap.memory==request.memory&&
           last_unmap.dma_offset==mapped_offset&&last_unmap.size==request.length);
    assert(unmap_calls==1U&&release_calls==2U);

    memset(&mapping,0,sizeof(mapping));wait_status=NVX_GSP_RMAPI_TIMEOUT;
    assert(nvx_gsp_dma_map(&manager,&mapping,&request,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_TIMEOUT);
    assert(mapping.stage==NVX_GSP_DMA_MAP_MAPPING&&
           mapping.operation.handle.valid);
    assert(nvx_gsp_dma_map_recover(&manager,&mapping,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_TIMEOUT);
    assert(mapping.stage==NVX_GSP_DMA_MAP_MAPPING);
    wait_status=NVX_GSP_RMAPI_RPC_ERROR;
    assert(nvx_gsp_dma_map_recover(&manager,&mapping,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_RPC_ERROR);
    assert(mapping.stage==NVX_GSP_DMA_MAP_MAPPING&&
           mapping.operation.handle.valid);
    wait_status=NVX_GSP_RMAPI_OK;
    assert(nvx_gsp_dma_map_recover(&manager,&mapping,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_OK);
    assert(mapping.stage==NVX_GSP_DMA_MAP_MAPPED&&
           mapping.dma_offset==mapped_offset);

    wait_status=NVX_GSP_RMAPI_TIMEOUT;
    assert(nvx_gsp_dma_unmap(&manager,&mapping,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_TIMEOUT);
    assert(mapping.stage==NVX_GSP_DMA_MAP_UNMAPPING);
    wait_status=NVX_GSP_RMAPI_OK;
    assert(nvx_gsp_dma_map_recover(&manager,&mapping,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_OK);
    assert(mapping.stage==NVX_GSP_DMA_MAP_IDLE);

    memset(&mapping,0,sizeof(mapping));result_status=NVX_GSP_RMAPI_RESPONSE_TOO_SMALL;
    assert(nvx_gsp_dma_map(&manager,&mapping,&request,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==
        NVX_GSP_RMAPI_RESPONSE_TOO_SMALL);
    assert(mapping.stage==NVX_GSP_DMA_MAP_IDLE&&mapping.dma_offset==0U);
    result_status=NVX_GSP_RMAPI_OK;

    memset(&mapping,0,sizeof(mapping));
    request.offset=UINT64_MAX-3U;request.length=8U;
    assert(nvx_gsp_dma_map(&manager,&mapping,&request,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==
        NVX_GSP_RMAPI_INVALID_ARGUMENT);
    request.offset=0x2000U;request.length=0x6000U;

    /* A local handle-release failure is retryable and must not remap. */
    release_status=NVX_GSP_RPC_TRANSPORT_ERROR;
    assert(nvx_gsp_dma_map(&manager,&mapping,&request,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_RPC_ERROR);
    assert(mapping.stage==NVX_GSP_DMA_MAP_MAP_RELEASE_PENDING&&
           mapping.operation.handle.valid);
    release_status=NVX_GSP_RPC_OK;
    unsigned before=map_calls;
    assert(nvx_gsp_dma_map_recover(&manager,&mapping,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_OK);
    assert(mapping.stage==NVX_GSP_DMA_MAP_MAPPED&&map_calls==before);
    assert(nvx_gsp_dma_unmap(&manager,&mapping,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_OK);
    return 0;
}
