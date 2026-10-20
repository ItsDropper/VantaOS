/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_preflight.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
static uint8_t elf[768],ucodes[256],boot_assets[3][512];
static int verify_stub(const char *p,const nvx_firmware_blob_t *b,void *c){(void)p;(void)b;(void)c;return 1;}
static void p16(size_t o,uint16_t v){elf[o]=(uint8_t)v;elf[o+1]=(uint8_t)(v>>8U);}
static void p32(size_t o,uint32_t v){for(unsigned i=0;i<4U;i++)elf[o+i]=(uint8_t)(v>>(i*8U));}
static void p64(size_t o,uint64_t v){for(unsigned i=0;i<8U;i++)elf[o+i]=(uint8_t)(v>>(i*8U));}
static void asset32(uint8_t *dest,size_t offset,uint32_t value){
 for(unsigned i=0U;i<4U;i++)dest[offset+i]=(uint8_t)(value>>(i*8U));
}
static void build_booter(uint8_t *dest){
 memset(dest,0,512U);
 asset32(dest,0U,0x10DEU);asset32(dest,4U,1U);asset32(dest,8U,512U);
 asset32(dest,12U,24U);asset32(dest,16U,184U);asset32(dest,20U,128U);
 asset32(dest,24U,60U);asset32(dest,28U,64U);asset32(dest,32U,124U);
 asset32(dest,36U,128U);asset32(dest,40U,132U);asset32(dest,44U,12U);
 asset32(dest,48U,144U);asset32(dest,52U,148U);asset32(dest,56U,36U);
 asset32(dest,124U,40U);asset32(dest,128U,0U);asset32(dest,132U,0x1234U);
 asset32(dest,136U,0x400U);asset32(dest,140U,1U);asset32(dest,144U,2U);
 asset32(dest,148U,0U);asset32(dest,152U,32U);asset32(dest,156U,32U);
 asset32(dest,160U,64U);asset32(dest,164U,1U);asset32(dest,168U,32U);
 asset32(dest,172U,32U);asset32(dest,176U,32U);asset32(dest,180U,16U);
}
static void build_gsp_bootloader(uint8_t *dest){
 memset(dest,0,512U);
 asset32(dest,0U,0x10DEU);asset32(dest,4U,1U);asset32(dest,8U,256U);
 asset32(dest,12U,24U);asset32(dest,16U,108U);asset32(dest,20U,128U);
 for(size_t i=0U;i<84U;i++)dest[24U+i]=(uint8_t)(i+1U);
 for(size_t i=0U;i<128U;i++)dest[108U+i]=(uint8_t)(0x80U+i);
}
static void build_elf(void){
 memset(elf,0,sizeof(elf));elf[0]=0x7F;elf[1]='E';elf[2]='L';elf[3]='F';elf[4]=2;elf[5]=1;elf[6]=1;
 p16(16,2);p16(18,243);p32(20,1);p64(24,0x1000);p64(32,64);p64(40,128);
 p16(52,64);p16(54,56);p16(56,1);p16(58,64);p16(60,5);p16(62,1);
 p32(64,1);p32(68,5);p64(72,640);p64(80,0x1000);p64(88,0x1000);p64(96,32);p64(104,64);p64(112,16);
 p32(196,3);p64(216,448);p64(224,50);p64(240,1);p32(256,11);p32(260,1);p64(280,512);p64(288,64);p64(304,16);
 p32(320,20);p32(324,1);p64(344,576);p64(352,11);p64(368,1);p32(384,31);p32(388,1);p64(408,592);p64(416,32);p64(432,16);
 memcpy(elf+448,"\0.shstrtab\0.fwimage\0.fwversion\0.fwsignature_ga10x\0",50U);memcpy(elf+576,"535.113.01",11U);
}
int main(void){
 nvx_gsp_bundle_t b={0};nvx_gsp_preflight_report_t report={0};b.family=NVX_FAMILY_AMPERE;b.acquired=1U;b.provider.verify=verify_stub;strcpy(b.chip,"ga106");strcpy(b.version,"535.113.01");build_elf();
 b.components[0]=(nvx_gsp_component_t){.blob={elf,sizeof(elf),0},.kind=NVX_GSP_RUNTIME,.acquired=1U};
 b.components[1]=(nvx_gsp_component_t){.blob={ucodes,sizeof(ucodes),0},.kind=NVX_GSP_UCODE_ARCHIVE,.acquired=1U};
 for(unsigned i=2U;i<4U;i++){build_booter(boot_assets[i-2U]);b.components[i]=(nvx_gsp_component_t){.blob={boot_assets[i-2U],sizeof(boot_assets[0]),0},.kind=(nvx_gsp_component_kind_t)i,.acquired=1U};}build_gsp_bootloader(boot_assets[2]);b.components[4]=(nvx_gsp_component_t){.blob={boot_assets[2],256U,0},.kind=NVX_GSP_BOOTLOADER,.acquired=1U};
 assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_OK);
 assert(report.runtime_verified&&report.ucode_archive_verified&&report.boot_assets_present);
 assert(report.runtime.machine==243U&&report.runtime.load_segment_count==1U);
 assert(report.runtime_bytes==sizeof(elf)&&report.ucode_archive_bytes==sizeof(ucodes));
 assert(report.booter_load_bytes==512U&&report.booter_unload_bytes==512U&&report.bootloader_bytes==256U);
 assert(report.gsp_bootloader.descriptor_size==84U&&report.gsp_bootloader.payload_offset==108U&&report.gsp_bootloader.payload_size==128U);
 uint8_t bootloader_stage[128],bootloader_backup[128];
 nvx_gsp_bootloader_info_t bootloader_info;
 assert(nvx_gsp_bootloader_prepare(&b.components[4],bootloader_stage,sizeof(bootloader_stage),&bootloader_info)==NVX_GSP_BOOTLOADER_OK);
 assert(bootloader_info.descriptor_size==84U&&bootloader_info.descriptor[0]==1U);
 assert(bootloader_stage[0]==0x80U&&bootloader_stage[127]==0xFFU);
 memset(bootloader_stage,0xA5U,sizeof(bootloader_stage));memcpy(bootloader_backup,bootloader_stage,sizeof(bootloader_stage));
 assert(nvx_gsp_bootloader_prepare(&b.components[4],bootloader_stage,sizeof(bootloader_stage)-1U,&bootloader_info)==NVX_GSP_BOOTLOADER_BUFFER_TOO_SMALL);
 assert(memcmp(bootloader_stage,bootloader_backup,sizeof(bootloader_stage))==0);
 assert(nvx_gsp_bootloader_prepare(&b.components[4],boot_assets[2]+108U,128U,&bootloader_info)==NVX_GSP_BOOTLOADER_INVALID_ARGUMENT);
 assert(report.booter_load.engine_id==0x400U&&report.booter_unload.ucode_id==1U);
 assert(report.firmware_image.offset==512U&&report.firmware_image.size==64U);
 assert(report.firmware_version.offset==576U&&strcmp(report.version,"535.113.01")==0);
 strcpy(b.chip,"ga100");assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_UNSUPPORTED_CHIP);
 strcpy(b.chip,"ga106");assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_OK);
 uint8_t load_stage[128],unload_stage[128],backup_load[128],backup_unload[128],overlap_stage[256];
 nvx_gsp_image_info_t load_info,unload_info;
 for(size_t i=0U;i<128U;i++){boot_assets[0][184U+i]=(uint8_t)i;boot_assets[1][184U+i]=(uint8_t)(0x80U+i);}
 for(size_t i=0U;i<32U;i++){
  boot_assets[0][60U+i]=(uint8_t)(0x10U+i);boot_assets[0][92U+i]=(uint8_t)(0x30U+i);
  boot_assets[1][60U+i]=(uint8_t)(0x50U+i);boot_assets[1][92U+i]=(uint8_t)(0x70U+i);
 }
 assert(nvx_gsp_preflight_stage_booters(&b,0U,load_stage,sizeof(load_stage),
        1U,unload_stage,sizeof(unload_stage),&load_info,&unload_info)==NVX_GSP_PREFLIGHT_OK);
 assert(load_info.data_size==128U&&unload_info.data_size==128U);
 assert(load_stage[0]==0U&&load_stage[40]==0x30U&&load_stage[71]==0x4FU);
 assert(unload_stage[0]==0x80U&&unload_stage[40]==0x50U&&unload_stage[71]==0x6FU);
 memset(load_stage,0xA5U,sizeof(load_stage));memset(unload_stage,0x5AU,sizeof(unload_stage));
 memcpy(backup_load,load_stage,sizeof(load_stage));memcpy(backup_unload,unload_stage,sizeof(unload_stage));
 assert(nvx_gsp_preflight_stage_booters(&b,2U,load_stage,sizeof(load_stage),
        0U,unload_stage,sizeof(unload_stage),&load_info,&unload_info)==NVX_GSP_PREFLIGHT_FUSE_VERSION_UNSUPPORTED);
 assert(memcmp(load_stage,backup_load,sizeof(load_stage))==0&&memcmp(unload_stage,backup_unload,sizeof(unload_stage))==0);
 assert(nvx_gsp_preflight_stage_booters(&b,0U,load_stage,sizeof(load_stage)-1U,
        0U,unload_stage,sizeof(unload_stage),&load_info,&unload_info)==NVX_GSP_PREFLIGHT_STAGING_TOO_SMALL);
 assert(memcmp(load_stage,backup_load,sizeof(load_stage))==0&&memcmp(unload_stage,backup_unload,sizeof(unload_stage))==0);
 assert(nvx_gsp_preflight_stage_booters(&b,0U,overlap_stage,128U,
        0U,overlap_stage+64U,128U,&load_info,&unload_info)==NVX_GSP_PREFLIGHT_STAGING_ALIAS);
 assert(nvx_gsp_preflight_stage_booters(&b,0U,load_stage,sizeof(load_stage),
        0U,unload_stage,sizeof(unload_stage),&load_info,&load_info)==NVX_GSP_PREFLIGHT_STAGING_ALIAS);
 assert(nvx_gsp_preflight_stage_booters(&b,0U,boot_assets[0]+184U,128U,
        0U,unload_stage,sizeof(unload_stage),&load_info,&unload_info)==NVX_GSP_PREFLIGHT_STAGING_ALIAS);
 boot_assets[0][0]=0U;assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_CONTAINER_INVALID);assert(report.runtime_bytes==0U);boot_assets[0][0]=0xDEU;
 boot_assets[1][0]=0U;assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_CONTAINER_INVALID);boot_assets[1][0]=0xDEU;
 boot_assets[2][0]=0U;assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_CONTAINER_INVALID);boot_assets[2][0]=0xDEU;
 assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_OK);
 strcpy(b.version,"535.113.02");assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_VERSION_MISMATCH);
 strcpy(b.version,"535.113.01");elf[586]=1U;assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_CONTAINER_INVALID);elf[586]=0U;
 b.components[4].acquired=0U;assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_COMPONENT_INVALID);assert(report.runtime_bytes==0U);b.components[4].acquired=1U;
 b.components[1].acquired=0U;assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_COMPONENT_INVALID);assert(report.runtime_bytes==0U);
 b.components[1].acquired=1U;elf[0]=0U;assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_RUNTIME_INVALID);
 assert(nvx_gsp_preflight_validate(0,&report)==NVX_GSP_PREFLIGHT_INVALID_ARGUMENT);
 assert(nvx_gsp_preflight_validate(&b,(nvx_gsp_preflight_report_t *)(void *)&b)==NVX_GSP_PREFLIGHT_INVALID_ARGUMENT);
 assert(nvx_gsp_preflight_validate(&b,0)==NVX_GSP_PREFLIGHT_INVALID_ARGUMENT);b.acquired=0U;
 assert(nvx_gsp_preflight_validate(&b,&report)==NVX_GSP_PREFLIGHT_BUNDLE_NOT_READY);return 0;
}
