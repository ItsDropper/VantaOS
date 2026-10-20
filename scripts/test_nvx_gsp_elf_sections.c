/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_elf.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
static uint8_t elf[768];
static void p16(size_t o,uint16_t v){elf[o]=(uint8_t)v;elf[o+1]=(uint8_t)(v>>8U);}
static void p32(size_t o,uint32_t v){for(unsigned i=0;i<4U;i++)elf[o+i]=(uint8_t)(v>>(i*8U));}
static void p64(size_t o,uint64_t v){for(unsigned i=0;i<8U;i++)elf[o+i]=(uint8_t)(v>>(i*8U));}
static void build(nvx_gsp_component_t *c)
{
 memset(elf,0,sizeof(elf));elf[0]=0x7F;elf[1]='E';elf[2]='L';elf[3]='F';elf[4]=2;elf[5]=1;elf[6]=1;
 p16(16,2);p16(18,243);p32(20,1);p64(24,0x1000);p64(32,64);p64(40,128);
 p16(52,64);p16(54,56);p16(56,1);p16(58,64);p16(60,5);p16(62,1);
 p32(64,1);p32(68,5);p64(72,640);p64(80,0x1000);p64(88,0x1000);p64(96,32);p64(104,64);p64(112,16);
 p32(196,3);p64(216,448);p64(224,50);p64(240,1);
 p32(256,11);p32(260,1);p64(280,512);p64(288,64);p64(304,16);
 p32(320,20);p32(324,1);p64(344,576);p64(352,11);p64(368,1);
 p32(384,31);p32(388,1);p64(408,592);p64(416,32);p64(432,16);
 memcpy(elf+448,"\0.shstrtab\0.fwimage\0.fwversion\0.fwsignature_ga10x\0",50U);
 memcpy(elf+576,"535.113.01",11U);
 *c=(nvx_gsp_component_t){.blob={elf,sizeof(elf),0},.kind=NVX_GSP_RUNTIME,.acquired=1U};
}
int main(void)
{
 nvx_gsp_component_t c={0};nvx_gsp_elf_section_t s={0};build(&c);
 assert(nvx_gsp_elf_find_section(&c,".fwimage",&s)==NVX_GSP_ELF_OK);
 assert(s.offset==512U&&s.size==64U&&s.type==1U&&s.index==2U&&s.data==elf+512U);
 assert(nvx_gsp_elf_find_section(&c,".fwversion",&s)==NVX_GSP_ELF_OK);
 assert(s.offset==576U&&s.size==11U&&strcmp((const char *)s.data,"535.113.01")==0);
 assert(nvx_gsp_elf_find_section(&c,".fwsignature_ga10x",&s)==NVX_GSP_ELF_OK);
 assert(s.offset==592U&&s.size==32U);
 assert(nvx_gsp_elf_find_section(&c,".missing",&s)==NVX_GSP_ELF_SECTION_NOT_FOUND);
 assert(nvx_gsp_elf_find_section(&c,".fwimage",(nvx_gsp_elf_section_t *)(void *)elf)==NVX_GSP_ELF_INVALID_ARGUMENT);
 p32(384,11U);
 assert(nvx_gsp_elf_find_section(&c,".fwimage",&s)==NVX_GSP_ELF_SECTION_AMBIGUOUS);
 build(&c);p32(260,8U);
 assert(nvx_gsp_elf_find_section(&c,".fwimage",&s)==NVX_GSP_ELF_SECTION_NO_DATA);
 build(&c);p64(280,UINT64_MAX);
 assert(nvx_gsp_elf_find_section(&c,".fwimage",&s)==NVX_GSP_ELF_SECTION_TABLE_INVALID);
 build(&c);p64(304,3U);
 assert(nvx_gsp_elf_find_section(&c,".fwimage",&s)==NVX_GSP_ELF_SECTION_TABLE_INVALID);
 c.kind=NVX_GSP_UCODE_ARCHIVE;
 assert(nvx_gsp_elf_find_section(&c,".fwimage",&s)==NVX_GSP_ELF_UNSUPPORTED_FORMAT);
 return 0;
}
