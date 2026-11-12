/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_context_dma.h"

#include <assert.h>
#include <string.h>

static nvx_gsp_rmapi_status_t alloc_submit_status, free_submit_status,
    wait_status;
static nvx_gsp_rpc_status_t release_status;
static unsigned alloc_calls, free_calls, wait_calls, release_calls;
static uint32_t allocated_class, allocated_parent, allocated_object;
static nvx_gsp_context_dma_params_t captured_params;

nvx_gsp_rmapi_status_t nvx_gsp_rm_alloc_submit(
    nvx_gsp_rpc_manager_t *manager,uint32_t client,uint32_t parent,
    uint32_t object,uint32_t class_id,uint32_t flags,const void *params,
    size_t params_size,void *scratch,size_t scratch_size,void *response,
    size_t response_capacity,nvx_gsp_rmapi_result_t *out)
{
    (void)manager;(void)client;(void)flags;(void)scratch;(void)scratch_size;
    (void)response;(void)response_capacity;
    assert(params_size==sizeof(captured_params));
    memcpy(&captured_params,params,sizeof(captured_params));
    allocated_parent=parent;allocated_object=object;allocated_class=class_id;
    alloc_calls++;out->function=NVX_GSP_RPC_FN_RM_ALLOC;
    out->response_size=20U;out->handle.valid=1U;
    return alloc_submit_status;
}
nvx_gsp_rmapi_status_t nvx_gsp_rm_free_submit(
    nvx_gsp_rpc_manager_t *manager,uint32_t root,uint32_t parent,
    uint32_t object,void *scratch,size_t scratch_size,void *response,
    size_t response_capacity,nvx_gsp_rmapi_result_t *out)
{
    (void)manager;(void)root;(void)scratch;(void)scratch_size;
    (void)response;(void)response_capacity;
    assert(parent==2U&&object==3U);free_calls++;
    out->function=NVX_GSP_RPC_FN_FREE;out->response_size=16U;
    out->handle.valid=1U;return free_submit_status;
}
nvx_gsp_rmapi_status_t nvx_gsp_rmapi_wait(
    nvx_gsp_rpc_manager_t *manager,nvx_gsp_rmapi_result_t *operation,
    void *scratch,size_t scratch_size,void *response,size_t response_size,
    uint32_t timeout,uint32_t interval,nvx_gsp_rmapi_delay_fn delay,
    void *delay_context)
{
    (void)manager;(void)operation;(void)scratch;(void)scratch_size;
    (void)response;(void)response_size;(void)timeout;(void)interval;
    (void)delay;(void)delay_context;wait_calls++;return wait_status;
}
nvx_gsp_rpc_status_t nvx_gsp_rpc_release(nvx_gsp_rpc_manager_t *manager,
    nvx_gsp_rpc_handle_t *handle)
{
    (void)manager;release_calls++;
    if(release_status==NVX_GSP_RPC_OK&&handle)
        *handle=(nvx_gsp_rpc_handle_t){0};
    return release_status;
}
static void delay(uint32_t ms,void *context){(void)ms;(void)context;}

int main(void)
{
    nvx_gsp_rpc_manager_t manager={0};
    nvx_gsp_context_dma_t object={0};
    uint8_t scratch[256],response[256];

    alloc_submit_status=free_submit_status=wait_status=NVX_GSP_RMAPI_OK;
    release_status=NVX_GSP_RPC_OK;

    assert(nvx_gsp_context_dma_create(&manager,&object,1U,2U,3U,4U,0U,
        0x55U,0U,0xFFFFU,scratch,sizeof(scratch),response,sizeof(response),
        100U,1U,delay,NULL)==NVX_GSP_RMAPI_OK);
    assert(object.stage==NVX_GSP_CONTEXT_DMA_CREATED);
    assert(allocated_class==NVX_GSP_RM_CLASS_CONTEXT_DMA);
    assert(allocated_parent==2U&&allocated_object==3U);
    assert(captured_params.h_subdevice==0U&&captured_params.flags==0x55U);
    assert(captured_params.h_memory==4U&&captured_params.offset==0U&&
           captured_params.limit==0xFFFFU&&captured_params.reserved==0U);
    assert(alloc_calls==1U&&release_calls==1U);
    assert(nvx_gsp_context_dma_create(&manager,&object,1U,2U,3U,4U,0U,
        0x55U,0U,0xFFFFU,scratch,sizeof(scratch),response,sizeof(response),
        100U,1U,delay,NULL)==NVX_GSP_RMAPI_INVALID_ARGUMENT);

    assert(nvx_gsp_context_dma_destroy(&manager,&object,scratch,sizeof(scratch),
        response,sizeof(response),100U,1U,delay,NULL)==NVX_GSP_RMAPI_OK);
    assert(object.stage==NVX_GSP_CONTEXT_DMA_IDLE&&free_calls==1U);

    /* Timed-out allocation remains owned until the RPC is actually complete. */
    memset(&object,0,sizeof(object));wait_status=NVX_GSP_RMAPI_TIMEOUT;
    assert(nvx_gsp_context_dma_create(&manager,&object,1U,2U,3U,4U,0U,
        0U,0U,0xFFFU,scratch,sizeof(scratch),response,sizeof(response),
        10U,1U,delay,NULL)==NVX_GSP_RMAPI_TIMEOUT);
    assert(object.stage==NVX_GSP_CONTEXT_DMA_ALLOC_PENDING&&
           object.operation.handle.valid);
    assert(nvx_gsp_context_dma_recover(&manager,&object,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_TIMEOUT);
    wait_status=NVX_GSP_RMAPI_RPC_ERROR;
    assert(nvx_gsp_context_dma_recover(&manager,&object,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_RPC_ERROR);
    assert(object.operation.handle.valid);
    wait_status=NVX_GSP_RMAPI_OK;
    assert(nvx_gsp_context_dma_recover(&manager,&object,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_OK);
    assert(object.stage==NVX_GSP_CONTEXT_DMA_CREATED);

    /* A remote free failure preserves the object and allows a safe retry. */
    wait_status=NVX_GSP_RMAPI_REMOTE_ERROR;
    assert(nvx_gsp_context_dma_destroy(&manager,&object,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==
        NVX_GSP_RMAPI_REMOTE_ERROR);
    assert(object.stage==NVX_GSP_CONTEXT_DMA_CREATED);
    wait_status=NVX_GSP_RMAPI_OK;

    /* A completed free whose local RPC handle cannot be released is retryable. */
    release_status=NVX_GSP_RPC_TRANSPORT_ERROR;
    assert(nvx_gsp_context_dma_destroy(&manager,&object,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_RPC_ERROR);
    assert(object.stage==NVX_GSP_CONTEXT_DMA_FREE_RELEASE_PENDING&&
           object.operation.handle.valid);
    unsigned free_before_recover=free_calls;
    release_status=NVX_GSP_RPC_OK;
    assert(nvx_gsp_context_dma_recover(&manager,&object,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_OK);
    assert(object.stage==NVX_GSP_CONTEXT_DMA_IDLE&&
           free_calls==free_before_recover);

    /* A failed allocation completion clears the attempted object identity. */
    memset(&object,0,sizeof(object));wait_status=NVX_GSP_RMAPI_REMOTE_ERROR;
    assert(nvx_gsp_context_dma_create(&manager,&object,1U,2U,3U,4U,0U,
        0U,0U,0xFFFU,scratch,sizeof(scratch),response,sizeof(response),
        10U,1U,delay,NULL)==NVX_GSP_RMAPI_REMOTE_ERROR);
    assert(object.stage==NVX_GSP_CONTEXT_DMA_IDLE&&
           object.context_dma_handle==0U);
    wait_status=NVX_GSP_RMAPI_OK;
    assert(nvx_gsp_context_dma_create(&manager,&object,1U,2U,3U,4U,0U,
        0U,0U,0xFFFU,scratch,sizeof(scratch),response,sizeof(response),
        10U,1U,delay,NULL)==NVX_GSP_RMAPI_OK);
    assert(object.stage==NVX_GSP_CONTEXT_DMA_CREATED);
    assert(nvx_gsp_context_dma_destroy(&manager,&object,scratch,sizeof(scratch),
        response,sizeof(response),10U,1U,delay,NULL)==NVX_GSP_RMAPI_OK);

    /* Reject aliased RM handles and object/RPC-buffer overlap. */
    unsigned before=alloc_calls;
    assert(nvx_gsp_context_dma_create(&manager,&object,1U,2U,3U,3U,0U,
        0U,0U,0xFFFU,scratch,sizeof(scratch),response,sizeof(response),
        10U,1U,delay,NULL)==NVX_GSP_RMAPI_INVALID_ARGUMENT);
    union {
        nvx_gsp_context_dma_t object;
        uint8_t bytes[sizeof(nvx_gsp_context_dma_t)];
    } alias;
    memset(&alias,0,sizeof(alias));
    assert(nvx_gsp_context_dma_create(&manager,&alias.object,1U,2U,3U,4U,0U,
        0U,0U,0xFFFU,alias.bytes,sizeof(alias.bytes),response,sizeof(response),
        10U,1U,delay,NULL)==NVX_GSP_RMAPI_INVALID_ARGUMENT);
    assert(alloc_calls==before);

    /* Reject inverted limits and corrupted state without touching RM. */
    assert(nvx_gsp_context_dma_create(&manager,&object,1U,2U,3U,4U,0U,
        0U,0x1000U,0xFFFU,scratch,sizeof(scratch),response,sizeof(response),
        10U,1U,delay,NULL)==NVX_GSP_RMAPI_INVALID_ARGUMENT);
    object.stage=0xFFU;
    assert(nvx_gsp_context_dma_create(&manager,&object,1U,2U,3U,4U,0U,
        0U,0U,0xFFFU,scratch,sizeof(scratch),response,sizeof(response),
        10U,1U,delay,NULL)==NVX_GSP_RMAPI_INVALID_ARGUMENT);
    assert(alloc_calls==before);
    return 0;
}
