/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_nvkv.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint64_t le64(const uint8_t *p)
{
    uint64_t v=0U;
    for (unsigned i=0U;i<8U;i++) v|=(uint64_t)p[i]<<(i*8U);
    return v;
}

int main(void)
{
    uint8_t bytes[128];
    nvx_gsp_nvkv_writer_t w={0};
    const uint32_t seq32[]={0x11223344U,0x55667788U,0xAABBCCDDU};
    const uint64_t seq64[]={UINT64_C(0x0123456789ABCDEF),UINT64_C(0xFEDCBA9876543210)};
    const uint8_t arr8[]={0x11U,0x22U,0x33U};
    const uint32_t arr32[]={1U,2U,3U};
    const uint64_t arr64[]={UINT64_C(0xDEADBEEFCAFEBABE)};
    size_t size;

    memset(bytes,0xA5,sizeof(bytes));
    assert(nvx_gsp_nvkv_begin(&w,bytes,sizeof(bytes))==NVX_GSP_NVKV_OK);
    assert(nvx_gsp_nvkv_put_imm32(&w,0U,1U,0x12345678U)==NVX_GSP_NVKV_OK);
    assert(le64(bytes)==(UINT64_C(0x12345678)<<32U|UINT64_C(1)));
    assert(nvx_gsp_nvkv_put_seq32(&w,2U,0x10U,seq32,3U)==NVX_GSP_NVKV_OK);
    assert((le64(bytes+8U)&UINT64_C(0xFFFFFFFF))==UINT64_C(0x00020010) + (UINT64_C(1)<<28U));
    assert(bytes[16U]==0x44U && bytes[17U]==0x33U && bytes[18U]==0x22U && bytes[19U]==0x11U);
    assert(bytes[20U]==0x88U && bytes[21U]==0x77U && bytes[22U]==0x66U && bytes[23U]==0x55U);
    assert(bytes[24U]==0xDDU && bytes[25U]==0xCCU && bytes[26U]==0xBBU && bytes[27U]==0xAAU);
    assert(bytes[28U]==0U && bytes[29U]==0U && bytes[30U]==0U && bytes[31U]==0U);
    assert(nvx_gsp_nvkv_put_seq64(&w,0U,0x20U,seq64,2U)==NVX_GSP_NVKV_OK);
    assert(nvx_gsp_nvkv_put_array8(&w,0U,0x30U,arr8,3U)==NVX_GSP_NVKV_OK);
    assert(bytes[nvx_gsp_nvkv_size_bytes(&w)-5U]==0U);
    assert(nvx_gsp_nvkv_put_array32(&w,0U,0x31U,arr32,3U)==NVX_GSP_NVKV_OK);
    assert(nvx_gsp_nvkv_put_array64(&w,0U,0x32U,arr64,1U)==NVX_GSP_NVKV_OK);
    size=nvx_gsp_nvkv_size_bytes(&w);
    assert(size!=0U && (size%8U)==0U);
    assert(nvx_gsp_nvkv_validate(bytes,size)==NVX_GSP_NVKV_OK);

    assert(nvx_gsp_nvkv_put_imm32(&w,0x1000U,1U,0U)==NVX_GSP_NVKV_INVALID_ARGUMENT);
    assert(nvx_gsp_nvkv_put_seq32(&w,0U,0xFFFFU,seq32,2U)==NVX_GSP_NVKV_INVALID_COUNT);
    assert(nvx_gsp_nvkv_put_array8(&w,0U,1U,NULL,1U)==NVX_GSP_NVKV_INVALID_COUNT);

    assert(nvx_gsp_nvkv_begin(&w,bytes,8U)==NVX_GSP_NVKV_OK);
    assert(nvx_gsp_nvkv_put_seq64(&w,0U,0U,seq64,1U)==NVX_GSP_NVKV_NO_SPACE);
    assert(nvx_gsp_nvkv_size_bytes(&w)==0U);
    assert(nvx_gsp_nvkv_validate(bytes,0U)==NVX_GSP_NVKV_INVALID_ARGUMENT);
    {
        const uint64_t malformed=(UINT64_C(0xFFFFFFFF)<<32U)|
                                 (UINT64_C(1)<<28U)|UINT64_C(1);
        for (unsigned i=0U;i<8U;i++) bytes[i]=(uint8_t)(malformed>>(i*8U));
        assert(nvx_gsp_nvkv_validate(bytes,8U)==NVX_GSP_NVKV_BAD_STREAM);
    }
    return 0;
}
