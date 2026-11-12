/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_channel.h"

#include <assert.h>
#include <string.h>

static nvx_gsp_rmapi_status_t alloc_status, bind_status, schedule_status,
    free_status, wait_status;
static unsigned calls[8], call_count;
static unsigned release_count;

static void record(unsigned call) { calls[call_count++]=call; }

nvx_gsp_rmapi_status_t nvx_gsp_rmapi_alloc_ampere_channel_sync(
    nvx_gsp_rpc_manager_t *m,uint32_t c,uint32_t d,uint32_t h,
    const nvx_gsp_channel_alloc_params_t *p,void *s,size_t ss,void *r,
    size_t rs,uint32_t t,uint32_t pi,nvx_gsp_rmapi_delay_fn delay,void *ctx,
    nvx_gsp_rmapi_result_t *out)
{
    (void)m;(void)c;(void)d;(void)h;(void)p;(void)s;(void)ss;(void)r;
    (void)rs;(void)t;(void)pi;(void)delay;(void)ctx;record(1U);
    if(alloc_status==NVX_GSP_RMAPI_TIMEOUT)
        out->handle.valid=1U;
    return alloc_status;
}
nvx_gsp_rmapi_status_t nvx_gsp_rmapi_bind_channel_sync(
    nvx_gsp_rpc_manager_t *m,uint32_t c,uint32_t h,uint32_t e,void *s,
    size_t ss,void *r,size_t rs,uint32_t t,uint32_t pi,
    nvx_gsp_rmapi_delay_fn delay,void *ctx,nvx_gsp_rmapi_result_t *out)
{
    (void)m;(void)c;(void)h;(void)s;(void)ss;(void)r;(void)rs;(void)t;
    (void)pi;(void)delay;(void)ctx;assert(e==NVX_GSP_RM_ENGINE_TYPE_COPY0);
    record(2U);if(bind_status==NVX_GSP_RMAPI_TIMEOUT)out->handle.valid=1U;
    return bind_status;
}
nvx_gsp_rmapi_status_t nvx_gsp_rmapi_schedule_channel_sync(
    nvx_gsp_rpc_manager_t *m,uint32_t c,uint32_t h,int enable,void *s,
    size_t ss,void *r,size_t rs,uint32_t t,uint32_t pi,
    nvx_gsp_rmapi_delay_fn delay,void *ctx,nvx_gsp_rmapi_result_t *out)
{
    (void)m;(void)c;(void)h;(void)s;(void)ss;(void)r;(void)rs;(void)t;
    (void)pi;(void)delay;(void)ctx;record(enable?3U:4U);
    if(schedule_status==NVX_GSP_RMAPI_TIMEOUT)out->handle.valid=1U;
    return schedule_status;
}
nvx_gsp_rmapi_status_t nvx_gsp_rm_free_sync(
    nvx_gsp_rpc_manager_t *m,uint32_t root,uint32_t parent,uint32_t object,
    void *s,size_t ss,void *r,size_t rs,uint32_t t,uint32_t pi,
    nvx_gsp_rmapi_delay_fn delay,void *ctx,nvx_gsp_rmapi_result_t *out)
{
    (void)m;(void)root;(void)s;(void)ss;(void)r;(void)rs;(void)t;(void)pi;
    (void)delay;(void)ctx;assert(parent==0x20U&&object==0x30U);
    record(5U);if(free_status==NVX_GSP_RMAPI_TIMEOUT)out->handle.valid=1U;
    return free_status;
}
nvx_gsp_rmapi_status_t nvx_gsp_rmapi_wait(
    nvx_gsp_rpc_manager_t *m,nvx_gsp_rmapi_result_t *op,void *s,size_t ss,
    void *r,size_t rs,uint32_t t,uint32_t pi,nvx_gsp_rmapi_delay_fn d,void *ctx)
{
    (void)m;(void)op;(void)s;(void)ss;(void)r;(void)rs;(void)t;(void)pi;
    (void)d;(void)ctx;return wait_status;
}
nvx_gsp_rpc_status_t nvx_gsp_rpc_release(nvx_gsp_rpc_manager_t *m,
    nvx_gsp_rpc_handle_t *h)
{
    (void)m;release_count++;if(h)*h=(nvx_gsp_rpc_handle_t){0};
    return NVX_GSP_RPC_OK;
}
static void delay(uint32_t ms,void *ctx){(void)ms;(void)ctx;}

int main(void)
{
    nvx_gsp_rpc_manager_t manager={0};
    nvx_gsp_copy_channel_t channel={0};
    nvx_gsp_channel_alloc_params_t params={0};
    uint8_t scratch[4096],response[4096];
    alloc_status=bind_status=schedule_status=free_status=wait_status=
        NVX_GSP_RMAPI_OK;

    assert(nvx_gsp_copy_channel_create(&manager,&channel,0x10U,0x20U,0x30U,
        &params,scratch,sizeof(scratch),response,sizeof(response),20U,1U,
        delay,NULL)==NVX_GSP_RMAPI_OK);
    assert(call_count==3U&&calls[0]==1U&&calls[1]==2U&&calls[2]==3U);
    assert(channel.allocated&&channel.bound&&channel.scheduled);

    call_count=0U;
    assert(nvx_gsp_copy_channel_destroy(&manager,&channel,scratch,
        sizeof(scratch),response,sizeof(response),20U,1U,delay,NULL)==
        NVX_GSP_RMAPI_OK);
    assert(call_count==2U&&calls[0]==4U&&calls[1]==5U);
    assert(!channel.allocated&&!channel.bound&&!channel.scheduled);

    memset(&channel,0,sizeof(channel));call_count=0U;
    alloc_status=NVX_GSP_RMAPI_TIMEOUT;
    assert(nvx_gsp_copy_channel_create(&manager,&channel,0x10U,0x20U,0x30U,
        &params,scratch,sizeof(scratch),response,sizeof(response),20U,1U,
        delay,NULL)==NVX_GSP_RMAPI_TIMEOUT);
    assert(channel.stage==NVX_GSP_CHANNEL_ALLOC_PENDING&&
           channel.operation.handle.valid&&!channel.allocated);
    wait_status=NVX_GSP_RMAPI_TIMEOUT;
    assert(nvx_gsp_copy_channel_recover(&manager,&channel,scratch,
        sizeof(scratch),response,sizeof(response),20U,1U,delay,NULL)==
        NVX_GSP_RMAPI_TIMEOUT);
    assert(channel.stage==NVX_GSP_CHANNEL_ALLOC_PENDING);
    wait_status=NVX_GSP_RMAPI_OK;alloc_status=NVX_GSP_RMAPI_OK;
    assert(nvx_gsp_copy_channel_recover(&manager,&channel,scratch,
        sizeof(scratch),response,sizeof(response),20U,1U,delay,NULL)==
        NVX_GSP_RMAPI_OK);
    assert(channel.allocated&&channel.stage==NVX_GSP_CHANNEL_IDLE);
    assert(nvx_gsp_copy_channel_create(&manager,&channel,0x10U,0x20U,0x30U,
        &params,scratch,sizeof(scratch),response,sizeof(response),20U,1U,
        delay,NULL)==NVX_GSP_RMAPI_OK);
    assert(channel.bound&&channel.scheduled);
    assert(nvx_gsp_copy_channel_destroy(&manager,&channel,scratch,
        sizeof(scratch),response,sizeof(response),20U,1U,delay,NULL)==
        NVX_GSP_RMAPI_OK);

    memset(&channel,0,sizeof(channel));call_count=0U;
    bind_status=NVX_GSP_RMAPI_REMOTE_ERROR;
    assert(nvx_gsp_copy_channel_create(&manager,&channel,0x10U,0x20U,0x30U,
        &params,scratch,sizeof(scratch),response,sizeof(response),20U,1U,
        delay,NULL)==NVX_GSP_RMAPI_REMOTE_ERROR);
    assert(call_count==3U&&calls[0]==1U&&calls[1]==2U&&calls[2]==5U);
    assert(!channel.allocated&&!channel.bound);

    bind_status=NVX_GSP_RMAPI_OK;
    memset(&channel,0,sizeof(channel));call_count=0U;
    assert(nvx_gsp_copy_channel_create(&manager,&channel,0x10U,0x20U,0x30U,
        &params,scratch,sizeof(scratch),response,sizeof(response),20U,1U,
        delay,NULL)==NVX_GSP_RMAPI_OK);
    schedule_status=NVX_GSP_RMAPI_REMOTE_ERROR;
    call_count=0U;
    assert(nvx_gsp_copy_channel_destroy(&manager,&channel,scratch,
        sizeof(scratch),response,sizeof(response),20U,1U,delay,NULL)==
        NVX_GSP_RMAPI_REMOTE_ERROR);
    assert(channel.scheduled);
    schedule_status=NVX_GSP_RMAPI_OK;
    assert(nvx_gsp_copy_channel_destroy(&manager,&channel,scratch,
        sizeof(scratch),response,sizeof(response),20U,1U,delay,NULL)==
        NVX_GSP_RMAPI_OK);
    assert(!channel.allocated&&!channel.scheduled);
    return 0;
}
