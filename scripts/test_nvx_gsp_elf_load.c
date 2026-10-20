/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_elf_load.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint8_t image[512];
static uint8_t target[128];
static int map_fails;
static int map_alias;

static void p16(size_t o, uint16_t v)
{ image[o]=(uint8_t)v; image[o+1U]=(uint8_t)(v>>8U); }
static void p32(size_t o, uint32_t v)
{ for (unsigned i=0U;i<4U;i++) image[o+i]=(uint8_t)(v>>(i*8U)); }
static void p64(size_t o, uint64_t v)
{ for (unsigned i=0U;i<8U;i++) image[o+i]=(uint8_t)(v>>(i*8U)); }

static nvx_gsp_component_t build(void)
{
    memset(image,0,sizeof(image));
    image[0]=0x7FU;image[1]='E';image[2]='L';image[3]='F';
    image[4]=2U;image[5]=1U;image[6]=1U;
    p16(16U,2U);p16(18U,243U);p32(20U,1U);p64(24U,0x1000U);
    p64(32U,64U);p64(40U,128U);p16(52U,64U);p16(54U,56U);
    p16(56U,1U);p16(58U,64U);p16(60U,3U);p16(62U,1U);
    p32(64U,1U);p32(68U,5U);p64(72U,384U);p64(80U,0x1000U);
    p64(88U,0x1000U);p64(96U,32U);p64(104U,64U);p64(112U,16U);
    p32(192U,1U);p32(196U,3U);p64(216U,320U);p64(224U,40U);p64(240U,1U);
    p32(256U,11U);p32(260U,1U);p64(280U,416U);p64(288U,32U);p64(304U,16U);
    memcpy(image+321U,".shstrtab",9U);memcpy(image+331U,".fwsignature_ga10x",18U);
    for (unsigned i=0U;i<32U;i++) image[384U+i]=(uint8_t)(i+1U);
    return (nvx_gsp_component_t){
        .blob={image,sizeof(image),0},.kind=NVX_GSP_RUNTIME,.acquired=1U
    };
}

static int map_destination(void *context,uint64_t address,size_t length,uint8_t **out)
{
    (void)context;
    if(map_fails||address!=0x1000U||length!=64U)return 0;
    *out=map_alias?image:target;
    return 1;
}

int main(void)
{
    nvx_gsp_component_t c=build();
    uint64_t entry=0U;
    memset(target,0xCC,sizeof(target));
    map_fails=0;map_alias=0;
    assert(nvx_gsp_elf_load_segments(&c,map_destination,0,&entry)==NVX_GSP_ELF_LOAD_OK);
    assert(entry==0x1000U);
    for(unsigned i=0U;i<32U;i++)assert(target[i]==(uint8_t)(i+1U));
    for(unsigned i=32U;i<64U;i++)assert(target[i]==0U);

    memset(target,0xCC,sizeof(target));entry=99U;map_fails=1;
    assert(nvx_gsp_elf_load_segments(&c,map_destination,0,&entry)==NVX_GSP_ELF_LOAD_DESTINATION_UNAVAILABLE);
    assert(entry==0U);
    for(unsigned i=0U;i<64U;i++)assert(target[i]==0xCCU);

    map_fails=0;map_alias=1;
    assert(nvx_gsp_elf_load_segments(&c,map_destination,0,&entry)==NVX_GSP_ELF_LOAD_DESTINATION_OVERLAP);
    assert(image[384U]==1U);

    map_alias=0;p16(16U,3U);entry=1U;
    assert(nvx_gsp_elf_load_segments(&c,map_destination,0,&entry)==NVX_GSP_ELF_LOAD_UNSUPPORTED_RELOCATION);
    assert(entry==0U);

    assert(nvx_gsp_elf_load_segments(0,map_destination,0,&entry)==NVX_GSP_ELF_LOAD_INVALID_ARGUMENT);
    return 0;
}
