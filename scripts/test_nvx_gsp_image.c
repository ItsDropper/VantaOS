/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_image.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
static uint8_t bytes[512];
static void put32(size_t o,uint32_t v) { bytes[o]=(uint8_t)v; bytes[o+1]=(uint8_t)(v>>8U); bytes[o+2]=(uint8_t)(v>>16U); bytes[o+3]=(uint8_t)(v>>24U); }
static void make_image(nvx_gsp_component_t *c) {
 memset(bytes,0,sizeof(bytes));
 put32(0,0x10DEU); put32(4,1U); put32(8,sizeof(bytes));
 put32(12,24U); put32(16,184U); put32(20,128U);
 /* 24-byte NVFW header + 36-byte secure-load header v2. */
 put32(24,60U); put32(28,64U); put32(32,124U); put32(36,128U);
 put32(40,132U); put32(44,12U); put32(48,144U); put32(52,148U); put32(56,36U);
 /* Packed signature array followed by patch location, patch_sig, metadata,
  * signature count, 36-byte one-application descriptor, then payload. */
 put32(124,40U); put32(128,0U); put32(132,1U);
 put32(136,1U); put32(140,1U); put32(144,2U);
 put32(148,0U); put32(152,32U); put32(156,32U); put32(160,64U);
 put32(164,1U); put32(168,32U); put32(172,32U);
 put32(176,32U); put32(180,16U);
 *c=(nvx_gsp_component_t){.blob={bytes,sizeof(bytes),0},.kind=NVX_GSP_BOOTER_LOAD,.acquired=1U};
}
int main(void) {
 nvx_gsp_component_t c={0}; nvx_gsp_image_info_t info; make_image(&c);
 assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_OK);
 assert(info.binary_magic==0x10DEU&&info.binary_size==512U&&info.data_offset==184U&&info.data_size==128U);
 assert(info.fuse_version==1U&&info.engine_id==1U&&info.ucode_id==1U);
 assert(info.signature_count==2U&&info.signature_size==32U&&info.signature_offset==60U&&info.patch_location==40U);
 assert(info.app_count==1U&&info.app0_size==32U&&info.app0_data_size==16U);
 uint8_t staged[128],backup[128];
 for(size_t i=0U;i<128U;i++)bytes[184U+i]=(uint8_t)i;
 for(size_t i=0U;i<32U;i++){bytes[60U+i]=(uint8_t)(0xA0U+i);bytes[92U+i]=(uint8_t)(0xC0U+i);}
 assert(nvx_gsp_image_prepare(&c,0U,staged,sizeof(staged))==NVX_GSP_IMAGE_OK);
 assert(staged[0]==0U&&staged[39]==39U&&staged[40]==0xC0U&&staged[71]==0xDFU&&staged[72]==72U);
 assert(nvx_gsp_image_prepare(&c,1U,staged,sizeof(staged))==NVX_GSP_IMAGE_OK);
 assert(staged[40]==0xA0U&&staged[71]==0xBFU);
 memset(staged,0x5AU,sizeof(staged));memcpy(backup,staged,sizeof(staged));
 assert(nvx_gsp_image_prepare(&c,2U,staged,sizeof(staged))==NVX_GSP_IMAGE_FUSE_VERSION_UNSUPPORTED);
 assert(memcmp(staged,backup,sizeof(staged))==0);
 assert(nvx_gsp_image_prepare(&c,0U,staged,sizeof(staged)-1U)==NVX_GSP_IMAGE_STAGING_TOO_SMALL);
 assert(memcmp(staged,backup,sizeof(staged))==0);
 assert(nvx_gsp_image_prepare(&c,0U,bytes,sizeof(bytes))==NVX_GSP_IMAGE_INVALID_ARGUMENT);
 c.kind=NVX_GSP_BOOTER_UNLOAD; assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_OK);
 c.kind=NVX_GSP_RUNTIME; assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_UNSUPPORTED_LAYOUT);
 c.kind=NVX_GSP_BOOTER_LOAD; c.blob.size=8U; assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_TRUNCATED);
 make_image(&c); put32(12,UINT32_MAX); assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_HEADER_INVALID);
 make_image(&c); put32(20,UINT32_MAX); assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_RANGE_INVALID);
 make_image(&c); put32(128,1U); assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_SIGNATURE_INVALID);
 make_image(&c); put32(144,0U); assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_SIGNATURE_INVALID);
 make_image(&c); put32(144,3U); assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_SIGNATURE_INVALID);
 make_image(&c); put32(144,16U); assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_SIGNATURE_INVALID);
 make_image(&c); put32(44,8U); assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_METADATA_INVALID);
 make_image(&c); put32(56,UINT32_MAX); assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_HEADER_INVALID);
 make_image(&c); put32(164,UINT32_MAX); assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_APP_INVALID);
 make_image(&c); put32(124,93U); assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_SIGNATURE_INVALID);
 assert(nvx_gsp_image_parse(0,&info)==NVX_GSP_IMAGE_INVALID_ARGUMENT); assert(nvx_gsp_image_parse(&c,0)==NVX_GSP_IMAGE_INVALID_ARGUMENT);
 make_image(&c);put32(144U,1U);put32(160U,80U);
 for(size_t i=0U;i<64U;i++)bytes[60U+i]=(uint8_t)(0x30U+i);
 for(size_t i=0U;i<128U;i++)bytes[184U+i]=(uint8_t)i;
 assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_OK&&info.signature_size==64U);
 assert(nvx_gsp_image_prepare(&c,1U,staged,sizeof(staged))==NVX_GSP_IMAGE_OK);
 assert(staged[40]==0x30U&&staged[103]==0x6FU);
 make_image(&c);put32(4U,2U);assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_UNSUPPORTED_VERSION);
 make_image(&c);put32(0U,0U);assert(nvx_gsp_image_parse(&c,&info)==NVX_GSP_IMAGE_BAD_MAGIC);
 make_image(&c);assert(nvx_gsp_image_parse(&c,(nvx_gsp_image_info_t *)(void *)bytes)==NVX_GSP_IMAGE_INVALID_ARGUMENT);
 assert(bytes[0]==0xDEU&&bytes[1]==0x10U&&bytes[2]==0U&&bytes[3]==0U);
 return 0;
}
