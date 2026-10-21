/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_rmapi.h"
#include "nvx_gsp_ring.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint32_t expected_function;
static uint8_t captured[4096];
static size_t captured_size;
static unsigned submissions;
static int auto_complete;
static nvx_gsp_rpc_status_t fake_state=NVX_GSP_RPC_PENDING;
static void *fake_response;
static size_t fake_response_capacity,fake_response_size;
static size_t history_size[16];
static uint8_t history[16][4096];

nvx_gsp_rpc_status_t nvx_gsp_rpc_pump(
    nvx_gsp_rpc_manager_t *manager,void *scratch,size_t scratch_size,
    nvx_gsp_rpc_view_t *unmatched)
{
    (void)manager;(void)scratch;(void)scratch_size;
    if(unmatched)memset(unmatched,0,sizeof(*unmatched));
    return auto_complete?NVX_GSP_RPC_COMPLETE:NVX_GSP_RPC_PENDING;
}

nvx_gsp_rpc_status_t nvx_gsp_rpc_query(
    nvx_gsp_rpc_manager_t *manager,const nvx_gsp_rpc_handle_t *handle,
    nvx_gsp_rpc_result_t *result)
{
    (void)manager;(void)handle;
    if(result){memset(result,0,sizeof(*result));result->status=fake_state;
        result->response_size=fake_response_size;}
    return fake_state;
}

nvx_gsp_rpc_status_t nvx_gsp_rpc_release(
    nvx_gsp_rpc_manager_t *manager,nvx_gsp_rpc_handle_t *handle)
{
    (void)manager;
    if(fake_state==NVX_GSP_RPC_PENDING)return NVX_GSP_RPC_BUSY;
    if(handle)*handle=(nvx_gsp_rpc_handle_t){0};
    return NVX_GSP_RPC_OK;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8U)|
           ((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U);
}

nvx_gsp_rpc_status_t nvx_gsp_rpc_submit(
    nvx_gsp_rpc_manager_t *manager,uint32_t version,uint32_t signature,
    uint32_t request_function,uint32_t response_function,
    const void *payload,size_t payload_size,void *scratch,size_t scratch_size,
    void *response,size_t response_capacity,nvx_gsp_rpc_handle_t *out_handle)
{
    (void)manager;(void)scratch;(void)scratch_size;
    assert(version==NVX_GSP_RPC_HEADER_VERSION_R535);
    assert(signature==NVX_GSP_RPC_SIGNATURE_R535);
    assert(request_function==expected_function);
    assert(response_function==expected_function);
    assert(payload_size<=sizeof(captured));
    memcpy(captured,payload,payload_size);captured_size=payload_size;
    if(submissions<16U){history_size[submissions]=payload_size;
        memcpy(history[submissions],payload,payload_size);}
    submissions++;
    fake_response=response;fake_response_capacity=response_capacity;
    fake_response_size=payload_size;
    if(auto_complete&&response&&response_capacity)memset(response,0,response_capacity);
    fake_state=auto_complete?NVX_GSP_RPC_COMPLETE:NVX_GSP_RPC_PENDING;
    *out_handle=(nvx_gsp_rpc_handle_t){.generation=7U,.slot=2U,.valid=1U};
    return NVX_GSP_RPC_OK;
}

static void no_delay(uint32_t ms,void *context)
{
    (void)ms;(void)context;
}

int main(void)
{
    nvx_gsp_rpc_manager_t manager={0};
    nvx_gsp_rmapi_result_t op={0};
    uint8_t scratch[4096],response[4096];
    const uint8_t params[]={0x11U,0x22U,0x33U,0x44U};
    uint32_t status=UINT32_MAX;

    expected_function=NVX_GSP_RPC_FN_RM_ALLOC;
    assert(nvx_gsp_rm_alloc_submit(&manager,1U,2U,3U,0x97U,0xA5U,
        params,sizeof(params),scratch,sizeof(scratch),response,sizeof(response),&op)==NVX_GSP_RMAPI_OK);
    assert(op.handle.valid && op.function==expected_function && captured_size==36U);
    assert(le32(captured+0U)==1U && le32(captured+4U)==2U);
    assert(le32(captured+8U)==3U && le32(captured+12U)==0x97U);
    assert(le32(captured+16U)==0U && le32(captured+20U)==4U);
    assert(le32(captured+24U)==0xA5U);
    assert(memcmp(captured+32U,params,sizeof(params))==0);
    memset(response,0,captured_size);response[16U]=0U;
    op.function=NVX_GSP_RPC_FN_RM_ALLOC;
    assert(nvx_gsp_rmapi_decode_status(&op,response,32U,&status)==NVX_GSP_RMAPI_OK);
    response[16U]=5U;
    assert(nvx_gsp_rmapi_decode_status(&op,response,32U,&status)==NVX_GSP_RMAPI_REMOTE_ERROR);
    assert(status==5U);

    expected_function=NVX_GSP_RPC_FN_RM_CONTROL;
    assert(nvx_gsp_rm_control_submit(&manager,0x10U,0x20U,0x30U,1U,2U,3U,
        params,sizeof(params),scratch,sizeof(scratch),response,sizeof(response),&op)==NVX_GSP_RMAPI_OK);
    assert(captured_size==44U && le32(captured+0U)==0x10U);
    assert(le32(captured+4U)==0x20U && le32(captured+8U)==0x30U);
    assert(le32(captured+12U)==0U && le32(captured+16U)==4U);
    assert(le32(captured+20U)==1U && le32(captured+24U)==2U);
    assert(le32(captured+28U)==3U && le32(captured+32U)==0U);
    assert(memcmp(captured+40U,params,sizeof(params))==0);
    op.function=NVX_GSP_RPC_FN_RM_CONTROL;
    memset(response,0,sizeof(response));
    assert(nvx_gsp_rmapi_decode_status(&op,response,40U,&status)==NVX_GSP_RMAPI_OK);

    expected_function=NVX_GSP_RPC_FN_FREE;
    assert(nvx_gsp_rm_free_submit(&manager,4U,5U,6U,scratch,sizeof(scratch),
        response,sizeof(response),&op)==NVX_GSP_RMAPI_OK);
    assert(captured_size==16U && le32(captured+0U)==4U);
    assert(le32(captured+4U)==5U && le32(captured+8U)==6U);
    assert(le32(captured+12U)==0U);
    op.function=NVX_GSP_RPC_FN_FREE;
    response[12U]=0U;
    assert(nvx_gsp_rmapi_decode_status(&op,response,16U,&status)==NVX_GSP_RMAPI_OK);

    assert(nvx_gsp_rm_alloc_submit(&manager,1U,2U,3U,4U,0U,params,
        NVX_GSP_RMAPI_MAX_PARAMS+1U,scratch,sizeof(scratch),response,
        sizeof(response),&op)==NVX_GSP_RMAPI_INVALID_ARGUMENT);
    {
        nvx_gsp_map_memory_dma_t map={0};
        nvx_gsp_unmap_memory_dma_t unmap={0};
        uint64_t mapped_offset=UINT64_MAX;
        map.client=1U;map.device=2U;map.dma=3U;map.memory=4U;
        map.offset=UINT64_C(0x1122334455667788);
        map.length=UINT64_C(0x10000);map.flags=5U;map.flags2=6U;
        map.kind_override=7U;map.dma_offset=UINT64_C(0xABCDEF00);
        expected_function=NVX_GSP_RPC_FN_MAP_MEMORY_DMA;
        assert(nvx_gsp_rm_map_memory_dma_submit(&manager,&map,scratch,sizeof(scratch),
            response,sizeof(response),&op)==NVX_GSP_RMAPI_OK);
        assert(captured_size==64U && le32(captured+0U)==1U && le32(captured+12U)==4U);
        assert(le32(captured+16U)==0x55667788U && le32(captured+20U)==0x11223344U);
        assert(le32(captured+24U)==0x10000U && le32(captured+32U)==5U);
        assert(le32(captured+36U)==6U && le32(captured+40U)==7U);
        assert(le32(captured+48U)==0xABCDEF00U);
        memset(response,0,sizeof(response));
        response[48U]=0x78U;response[49U]=0x56U;response[50U]=0x34U;response[51U]=0x12U;
        op.function=NVX_GSP_RPC_FN_MAP_MEMORY_DMA;
        assert(nvx_gsp_rm_map_memory_dma_result(&op,response,60U,&mapped_offset)==NVX_GSP_RMAPI_OK);
        assert(mapped_offset==UINT64_C(0x12345678));

        unmap.client=1U;unmap.device=2U;unmap.dma=3U;unmap.memory=4U;
        unmap.flags=8U;unmap.dma_offset=UINT64_C(0x1000);unmap.size=UINT64_C(0x2000);
        expected_function=NVX_GSP_RPC_FN_UNMAP_MEMORY_DMA;
        assert(nvx_gsp_rm_unmap_memory_dma_submit(&manager,&unmap,scratch,sizeof(scratch),
            response,sizeof(response),&op)==NVX_GSP_RMAPI_OK);
        assert(captured_size==48U && le32(captured+0U)==1U && le32(captured+16U)==8U);
        assert(le32(captured+24U)==0x1000U && le32(captured+32U)==0x2000U);
        op.function=NVX_GSP_RPC_FN_UNMAP_MEMORY_DMA;
        memset(response,0,sizeof(response));
        assert(nvx_gsp_rmapi_decode_status(&op,response,44U,&status)==NVX_GSP_RMAPI_OK);
    }
    assert(submissions==5U);

    {
        nvx_gsp_rm_core_objects_t objects;
        auto_complete=1;
        expected_function=NVX_GSP_RPC_FN_RM_ALLOC;
        assert(nvx_gsp_rm_create_core_objects(&manager,0x10U,0x20U,0x30U,
            0U,UINT64_C(1)<<40U,(UINT64_C(1)<<48U)-1U,scratch,sizeof(scratch),
            response,sizeof(response),20U,1U,no_delay,NULL,&objects)==NVX_GSP_RMAPI_OK);
        assert(objects.client_created&&objects.device_created&&objects.vaspace_created);
        assert(objects.in_flight_stage==0U);
        assert(history_size[5]==152U);
        assert(le32(history[5]+0U)==0U && le32(history[5]+8U)==0x10U);
        assert(le32(history[5]+12U)==NVX_GSP_RM_CLASS_ROOT);
        assert(le32(history[5]+20U)==120U);
        assert(le32(history[5]+32U)==0x10U);
        assert(memcmp(history[5]+40U,"VantaOS",7U)==0);
        assert(history_size[6]==88U);
        assert(le32(history[6]+8U)==0x20U);
        assert(le32(history[6]+12U)==NVX_GSP_RM_CLASS_DEVICE_0);
        assert(le32(history[6]+20U)==56U);
        assert(le32(history[6]+48U)==0U);
        assert(le32(history[6]+56U)==0U);
        assert(history_size[7]==56U);
        assert(le32(history[7]+8U)==0x30U);
        assert(le32(history[7]+12U)==NVX_GSP_RM_CLASS_MEMORY_VIRTUAL);
        assert(le32(history[7]+20U)==24U);
        assert(le32(history[7]+40U)==0xFFFFFFFFU);
        assert(fake_response==response&&fake_response_capacity==sizeof(response));
        expected_function=NVX_GSP_RPC_FN_RM_CONTROL;
        assert(nvx_gsp_rmapi_bind_channel_sync(&manager,0x10U,0x40U,0x17U,
            scratch,sizeof(scratch),response,sizeof(response),20U,1U,
            no_delay,NULL,&op)==NVX_GSP_RMAPI_OK);
        assert(history_size[8]==44U);
        assert(le32(history[8]+8U)==NVX_GSP_RM_CTRL_BIND_CHANNEL);
        assert(le32(history[8]+40U)==0x17U);
        assert(nvx_gsp_rmapi_schedule_channel_sync(&manager,0x10U,0x40U,1,
            scratch,sizeof(scratch),response,sizeof(response),20U,1U,
            no_delay,NULL,&op)==NVX_GSP_RMAPI_OK);
        assert(history_size[9]==52U);
        assert(le32(history[9]+8U)==NVX_GSP_RM_CTRL_GPFIFO_SCHEDULE);
        assert(le32(history[9]+40U)==1U);
        assert(le32(history[9]+44U)==0U&&le32(history[9]+48U)==0U);
        {
            nvx_gsp_channel_alloc_params_t channel_params={0};
            channel_params.h_object_error=0x11U;
            channel_params.gpfifo_offset=UINT64_C(0x100000);
            channel_params.gpfifo_entries=64U;
            channel_params.h_vaspace=0x30U;
            channel_params.engine_type=0x17U;
            expected_function=NVX_GSP_RPC_FN_RM_ALLOC;
            assert(nvx_gsp_rmapi_alloc_ampere_channel_sync(&manager,0x10U,0x20U,
                0x41U,&channel_params,scratch,sizeof(scratch),response,
                sizeof(response),20U,1U,no_delay,NULL,&op)==NVX_GSP_RMAPI_OK);
            assert(history_size[10]==408U);
            assert(le32(history[10]+8U)==0x41U);
            assert(le32(history[10]+12U)==NVX_GSP_RM_CLASS_AMPERE_CHANNEL_GPFIFO_A);
            assert(le32(history[10]+20U)==376U);
            assert(le32(history[10]+32U)==0x11U);
            assert(le32(history[10]+40U)==0x100000U);
            assert(le32(history[10]+48U)==64U);
            assert(le32(history[10]+60U)==0x30U);
            assert(le32(history[10]+168U)==0x17U);
        }
        auto_complete=0;
    }
    return 0;
}
