/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_preflight.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
static uint8_t elf[512],ucodes[256],logs[64];
static int verify_stub(const char *p,const nvx_firmware_blob_t *b,void *c)
{(void)p;(void)b;(void)c;return 1;}
static void p16(size_t o,uint16_t v){elf[o]=(uint8_t)v;elf[o+1]=(uint8_t)(v>>8U);}
static void p32(size_t o,uint32_t v){for(unsigned i=0;i<4U;i++)elf[o+i]=(uint8_t)(v>>(i*8U));}
static void p64(size_t o,uint64_t v){for(unsigned i=0;i<8U;i++)elf[o+i]=(uint8_t)(v>>(i*8U));}
static void build_elf(void)
{
 memset(elf,0,sizeof(elf));elf[0]=0x7F;elf[1]='E';elf[2]='L';elf[3]='F';elf[4]=2;elf[5]=1;elf[6]=1;
 p16(16,2);p16(18,243);p32(20,1);p64(24,0x1000);p64(32,64);p64(40,128);
 p16(52,64);p16(54,56);p16(56,1);p16(58,64);p16(60,3);p16(62,1);
 p32(64,1);p32(68,5);p64(72,384);p64(80,0x1000);p64(88,0x1000);p64(96,32);p64(104,64);p64(112,16);
 p32(192,1);p32(196,3);p64(216,320);p64(224,40);p64(240,1);
 p32(256,11);p32(260,1);p64(280,416);p64(288,32);p64(304,16);
 memcpy(elf+321,".shstrtab",9);memcpy(elf+331,NVX_GSP_GA10X_SIGNATURE_SECTION,18);
}
int main(void)
{
 nvx_gsp_bundle_t b={0};nvx_gsp_preflight_report_t report={0};
 b.family=NVX_FAMILY_AMPERE;b.acquired=1U;b.provider.verify=verify_stub;
 build_elf();
 b.components[0]=(nvx_gsp_component_t){.blob={elf,sizeof(elf),0},.kind=NVX_GSP_RUNTIME,.acquired=1U};
 b.components[1]=(nvx_gsp_component_t){.blob={ucodes,sizeof(ucodes),0},.kind=NVX_GSP_UCODE_ARCHIVE,.acquired=1U};
 assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_OK);
 assert(report.runtime_verified&&report.ucode_archive_verified&&!report.optional_log_present);
 assert(report.runtime.machine==243U&&report.runtime.load_segment_count==1U);
 assert(report.runtime_bytes==sizeof(elf)&&report.ucode_archive_bytes==sizeof(ucodes));
 b.components[2]=(nvx_gsp_component_t){.blob={logs,sizeof(logs),0},.kind=NVX_GSP_LOG,.acquired=1U};
 assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_OK);
 assert(report.optional_log_present&&report.log_bytes==sizeof(logs));
 b.components[1].acquired=0U;
 assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_COMPONENT_INVALID);
 assert(report.runtime_bytes==0U);
 b.components[1].acquired=1U;elf[0]=0U;
 assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_RUNTIME_INVALID);
 assert(nvx_gsp_preflight_validate(0,&report)==NVX_GSP_PREFLIGHT_INVALID_ARGUMENT);
 assert(nvx_gsp_preflight_validate(&b,0)==NVX_GSP_PREFLIGHT_INVALID_ARGUMENT);
 b.acquired=0U;
 assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_BUNDLE_NOT_READY);
 return 0;
}
