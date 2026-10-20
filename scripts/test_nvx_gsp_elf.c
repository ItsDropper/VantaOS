/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_elf.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
static uint8_t bytes[512];
static void p16(size_t o,uint16_t v){bytes[o]=(uint8_t)v;bytes[o+1]=(uint8_t)(v>>8U);}
static void p32(size_t o,uint32_t v){for(unsigned i=0;i<4;i++)bytes[o+i]=(uint8_t)(v>>(i*8U));}
static void p64(size_t o,uint64_t v){for(unsigned i=0;i<8;i++)bytes[o+i]=(uint8_t)(v>>(i*8U));}
static void build(nvx_gsp_component_t *c){
 memset(bytes,0,sizeof(bytes)); bytes[0]=0x7F;bytes[1]='E';bytes[2]='L';bytes[3]='F';bytes[4]=2;bytes[5]=1;bytes[6]=1;
 p16(16,2);p16(18,243);p32(20,1);p64(24,0x1000);p64(32,64);p64(40,128);p32(48,0);p16(52,64);p16(54,56);p16(56,1);p16(58,64);p16(60,3);p16(62,1);
 p32(64,1);p32(68,5);p64(72,384);p64(80,0x1000);p64(88,0x1000);p64(96,32);p64(104,64);p64(112,16);
 p32(192,1);p32(196,3);p64(216,320);p64(224,40);p64(240,1);
 p32(256,11);p32(260,1);p64(280,416);p64(288,32);p64(304,16);
 memcpy(bytes+321,".shstrtab",9);memcpy(bytes+331,".fwsignature_ga10x",18);
 *c=(nvx_gsp_component_t){.blob={bytes,sizeof(bytes),0},.kind=NVX_GSP_RUNTIME,.acquired=1U};
}
int main(void){
 nvx_gsp_component_t c={0};nvx_gsp_elf_info_t info;build(&c);
 assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_OK);
 assert(info.machine==243U&&info.type==2U&&info.entry==0x1000U);
 assert(info.signature_offset==416U&&info.signature_size==32U&&info.load_segment_count==1U);
 assert(info.load_segments[0].file_offset==384U&&info.load_segments[0].memory_size==64U);
 assert(nvx_gsp_elf_parse(&c,".fwsignature_ga100",&info)==NVX_GSP_ELF_SIGNATURE_MISSING);
 c.kind=NVX_GSP_BOOTER_LOAD;assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_UNSUPPORTED_FORMAT);
 c.kind=NVX_GSP_RUNTIME;c.blob.size=63U;assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_TRUNCATED);
 build(&c);bytes[0]=0;assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_BAD_MAGIC);
 build(&c);bytes[4]=1;assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_UNSUPPORTED_FORMAT);
 build(&c);p16(18,3);assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_UNSUPPORTED_FORMAT);
 build(&c);p64(32,UINT64_MAX);assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_PROGRAM_TABLE_INVALID);
 build(&c);p64(40,UINT64_MAX);assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_SECTION_TABLE_INVALID);
 build(&c);p16(54,32);assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_HEADER_INVALID);
 build(&c);p64(96,80);assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_LOAD_SEGMENT_INVALID);
 build(&c);p64(72,UINT64_MAX);assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_LOAD_SEGMENT_INVALID);
 build(&c);p64(24,0x2000);assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_ENTRY_INVALID);
 build(&c);p16(62,5);assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_SECTION_TABLE_INVALID);
 build(&c);p32(196,1);assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_STRING_TABLE_INVALID);
 build(&c);p64(288,0);assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_SIGNATURE_INVALID);
 build(&c);memset(bytes+320,'A',40);assert(nvx_gsp_elf_parse(&c,".fwsignature_ga10x",&info)==NVX_GSP_ELF_STRING_TABLE_INVALID);
 assert(nvx_gsp_elf_parse(0,".fwsignature_ga10x",&info)==NVX_GSP_ELF_INVALID_ARGUMENT);
 assert(nvx_gsp_elf_parse(&c,0,&info)==NVX_GSP_ELF_INVALID_ARGUMENT);
 return 0;
}
