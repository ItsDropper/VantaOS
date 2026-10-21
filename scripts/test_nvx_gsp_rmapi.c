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
    (void)manager;(void)scratch;(void)scratch_size;(void)response;(void)response_capacity;
    assert(version==NVX_GSP_RPC_HEADER_VERSION_R535);
    assert(signature==NVX_GSP_RPC_SIGNATURE_R535);
    assert(request_function==expected_function);
    assert(response_function==expected_function);
    assert(payload_size<=sizeof(captured));
    memcpy(captured,payload,payload_size);captured_size=payload_size;submissions++;
    *out_handle=(nvx_gsp_rpc_handle_t){.generation=7U,.slot=2U,.valid=1U};
    return NVX_GSP_RPC_OK;
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
    assert(le32(captured+16U)==UINT32_MAX && le32(captured+20U)==4U);
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
    assert(le32(captured+12U)==UINT32_MAX && le32(captured+16U)==4U);
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
    assert(le32(captured+12U)==UINT32_MAX);
    op.function=NVX_GSP_RPC_FN_FREE;
    response[12U]=0U;
    assert(nvx_gsp_rmapi_decode_status(&op,response,16U,&status)==NVX_GSP_RMAPI_OK);

    assert(nvx_gsp_rm_alloc_submit(&manager,1U,2U,3U,4U,0U,params,
        NVX_GSP_RMAPI_MAX_PARAMS+1U,scratch,sizeof(scratch),response,
        sizeof(response),&op)==NVX_GSP_RMAPI_INVALID_ARGUMENT);
    assert(submissions==3U);
    return 0;
}
