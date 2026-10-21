/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_static_info.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    uint8_t bytes[256];
    nvx_gsp_nvkv_writer_t w={0};
    nvx_gsp_static_info_t info;
    size_t size;
    const uint64_t fb_values[]={UINT64_C(0x100000000),UINT64_C(0x13FFFFFFF),
                                UINT64_C(0x10000000)};
    const uint8_t name[]="NVIDIA Test GPU";
    const uint8_t short_name[]="TEST";

    assert(nvx_gsp_nvkv_begin(&w,bytes,sizeof(bytes))==NVX_GSP_NVKV_OK);
    assert(nvx_gsp_nvkv_put_imm32(&w,0U,0x0001U,0x1234U)==NVX_GSP_NVKV_OK);
    assert(nvx_gsp_nvkv_put_imm32(&w,0U,0x0010U,1U)==NVX_GSP_NVKV_OK);
    assert(nvx_gsp_nvkv_put_seq64(&w,0U,0x1010U,fb_values,3U)==NVX_GSP_NVKV_OK);
    assert(nvx_gsp_nvkv_put_seq64(&w,0U,0x1000U,
        (const uint64_t[]){UINT64_C(0x45),UINT64_C(0x102),UINT64_C(0x3)},3U)==NVX_GSP_NVKV_OK);
    assert(nvx_gsp_nvkv_put_array8(&w,0U,0x2000U,name,sizeof(name)-1U)==NVX_GSP_NVKV_OK);
    assert(nvx_gsp_nvkv_put_array8(&w,0U,0x2001U,short_name,sizeof(short_name)-1U)==NVX_GSP_NVKV_OK);
    size=nvx_gsp_nvkv_size_bytes(&w);
    assert(nvx_gsp_static_info_decode(bytes,size,&info)==NVX_GSP_NVKV_OK);
    assert((info.present&NVX_GSP_SC_BOARD_ID)!=0U && info.board_id==0x1234U);
    assert((info.present&NVX_GSP_SC_FB_REGION_COUNT)!=0U && info.fb_region_count==1U);
    assert((info.present&NVX_GSP_SC_FB_LENGTH)!=0U && info.fb_length==fb_values[0]);
    assert((info.present&NVX_GSP_SC_BRANDING)!=0U && info.branding==0x45U);
    assert((info.present&NVX_GSP_SC_MISC_FEATURES)!=0U && info.misc_features==0x102U);
    assert((info.present&NVX_GSP_SC_POWER_FEATURES)!=0U && info.power_features==0x3U);
    assert(info.fb_regions[0].has_base && info.fb_regions[0].base==fb_values[1]);
    assert(info.fb_regions[0].has_limit && info.fb_regions[0].limit==fb_values[2]);
    assert(strcmp(info.gpu_name,"NVIDIA Test GPU")==0);
    assert(strcmp(info.gpu_short_name,"TEST")==0);

    /* A malicious region index cannot escape the bounded output model. */
    assert(nvx_gsp_nvkv_begin(&w,bytes,sizeof(bytes))==NVX_GSP_NVKV_OK);
    assert(nvx_gsp_nvkv_put_seq64(&w,17U,0x1011U,fb_values,1U)==NVX_GSP_NVKV_OK);
    size=nvx_gsp_nvkv_size_bytes(&w);
    assert(nvx_gsp_static_info_decode(bytes,size,&info)==NVX_GSP_NVKV_BAD_STREAM);
    return 0;
}
