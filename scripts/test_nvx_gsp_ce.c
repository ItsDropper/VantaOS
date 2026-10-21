/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_ce.h"

#include <assert.h>
#include <stdint.h>

static uint32_t header(uint32_t method,uint32_t count,uint32_t subchannel)
{
    return (UINT32_C(1)<<29U)|(count<<16U)|(subchannel<<13U)|(method>>2U);
}

int main(void)
{
    volatile uint32_t command[NVX_GSP_CE_COPY_COMMAND_DWORDS]={0};
    size_t bytes=0U;
    const uint64_t src=UINT64_C(0x12345678000);
    const uint64_t dst=UINT64_C(0x12345679000);
    const uint64_t completion=UINT64_C(0x1234567A000);
    assert(nvx_gsp_ce_build_copy(command,sizeof(command),0x77U,2U,src,dst,
        completion,4096U,0x00C0FFEEU,&bytes)==NVX_GSP_CE_OK);
    assert(bytes==NVX_GSP_CE_COPY_COMMAND_BYTES);
    assert(command[0]==header(0U,1U,2U)&&command[1]==0x77U);
    assert(command[2]==header(0x240U,3U,2U));
    assert(command[3]==((completion>>32U)&UINT64_C(0x1FFFF)));
    assert(command[4]==(uint32_t)completion&&command[5]==0x00C0FFEEU);
    assert(command[6]==header(0x400U,4U,2U));
    assert(command[7]==((src>>32U)&UINT64_C(0x1FFFF)));
    assert(command[8]==(uint32_t)src);
    assert(command[9]==((dst>>32U)&UINT64_C(0x1FFFF)));
    assert(command[10]==(uint32_t)dst);
    assert(command[11]==header(0x418U,2U,2U));
    assert(command[12]==4096U&&command[13]==1U);
    assert(command[14]==header(0x300U,1U,2U));
    assert(command[15]==0x18EU);

    assert(nvx_gsp_ce_build_copy(command,sizeof(command),0x77U,8U,src,dst,
        completion,4096U,1U,&bytes)==NVX_GSP_CE_INVALID_ARGUMENT);
    assert(nvx_gsp_ce_build_copy(command,sizeof(command),0x77U,0U,
        NVX_GSP_CE_GPUVA_LIMIT,dst,completion,4U,1U,&bytes)==NVX_GSP_CE_BAD_GPU_ADDRESS);
    assert(nvx_gsp_ce_build_copy(command,sizeof(command),0x77U,0U,src,dst,
        completion,0U,1U,&bytes)==NVX_GSP_CE_BAD_GPU_ADDRESS);
    assert(nvx_gsp_ce_build_copy(command,16U,0x77U,0U,src,dst,
        completion,4U,1U,&bytes)==NVX_GSP_CE_NO_SPACE);
    return 0;
}
