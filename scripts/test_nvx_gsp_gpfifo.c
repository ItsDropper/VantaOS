/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_gpfifo.h"
#include "nvx_gsp_ce.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct simulated_completion {
    volatile uint32_t *gp_get;
    volatile uint32_t *completion;
    uint32_t expected_get;
    uint32_t expected_value;
} simulated_completion_t;

static void complete_after_poll(uint32_t ms,void *context)
{
    simulated_completion_t *s=(simulated_completion_t *)context;
    (void)ms;
    *s->gp_get=s->expected_get;
    *s->completion=s->expected_value;
}

static void no_delay(uint32_t ms,void *context)
{
    (void)ms;(void)context;
}

int main(void)
{
    volatile uint32_t entries[8]={0};
    volatile uint32_t gp_get=0U,gp_put=0U,doorbell=0U;
    volatile uint32_t completion=0U;
    nvx_gsp_gpfifo_channel_t channel={0};
    uint32_t put=UINT32_MAX;
    const uint64_t command_va=UINT64_C(0x1234567800);
    const size_t command_bytes=64U;
    const uint32_t expected0=(uint32_t)((command_va>>2U)&UINT64_C(0x3FFFFFFF));
    const uint32_t expected1=(uint32_t)((command_va>>32U)&UINT64_C(0xFF))|
                              ((uint32_t)(command_bytes/4U)<<10U);

    assert(nvx_gsp_gpfifo_channel_init(&channel,entries,sizeof(entries),
        &gp_get,&gp_put,&doorbell,0xBEEFU,4U)==NVX_GSP_GPFIFO_OK);
    assert(channel.initialized&&channel.put_index==0U);
    assert(nvx_gsp_gpfifo_submit(&channel,command_va,command_bytes,&put)==NVX_GSP_GPFIFO_OK);
    assert(put==1U&&gp_put==1U&&doorbell==0xBEEFU);
    assert(entries[0]==expected0&&entries[1]==expected1);

    gp_get=1U;
    assert(nvx_gsp_gpfifo_wait_get(&channel,1U,5U,1U,no_delay,NULL)==NVX_GSP_GPFIFO_OK);
    completion=0x1234U;
    assert(nvx_gsp_gpfifo_wait_completion(&completion,0x1234U,5U,1U,
        no_delay,NULL)==NVX_GSP_GPFIFO_OK);
    assert(nvx_gsp_gpfifo_wait_completion(&completion,0x9999U,2U,1U,
        no_delay,NULL)==NVX_GSP_GPFIFO_TIMEOUT);

    /* A next put equal to GET means the ring has no free entry. */
    gp_get=2U;
    assert(nvx_gsp_gpfifo_submit(&channel,command_va,command_bytes,&put)==
           NVX_GSP_GPFIFO_QUEUE_FULL);
    assert(gp_put==1U);

    {
        volatile uint32_t nop_command=UINT32_C(0xFFFFFFFF);
        simulated_completion_t sim={&gp_get,&completion,1U,0U};
        gp_get=0U;gp_put=0U;doorbell=0U;
        assert(nvx_gsp_gpfifo_channel_init(&channel,entries,sizeof(entries),
            &gp_get,&gp_put,&doorbell,0xBEEFU,4U)==NVX_GSP_GPFIFO_OK);
        assert(nvx_gsp_gpfifo_submit_nop_probe(&channel,&nop_command,
            sizeof(nop_command),command_va,5U,1U,complete_after_poll,&sim,&put)==
            NVX_GSP_GPFIFO_OK);
        assert(nop_command==0U&&put==1U&&gp_put==1U);
    }

    /* The combined path distinguishes GP_GET consumption from GPU completion. */
    gp_get=0U;gp_put=0U;doorbell=0U;completion=0U;
    assert(nvx_gsp_gpfifo_channel_init(&channel,entries,sizeof(entries),
        &gp_get,&gp_put,&doorbell,0xBEEFU,4U)==NVX_GSP_GPFIFO_OK);
    {
        simulated_completion_t sim={&gp_get,&completion,1U,0x55AAU};
        assert(nvx_gsp_gpfifo_submit_and_wait(&channel,command_va,command_bytes,
            &completion,sim.expected_value,5U,1U,complete_after_poll,&sim,&put)==
            NVX_GSP_GPFIFO_OK);
        assert(put==1U&&gp_put==1U&&doorbell==0xBEEFU);
        assert(completion==0x55AAU);
    }

    assert(nvx_gsp_gpfifo_submit(&channel,NVX_GSP_GPFIFO_ADDRESS_LIMIT,
        command_bytes,&put)==NVX_GSP_GPFIFO_BAD_COMMAND_RANGE);
    assert(nvx_gsp_gpfifo_submit(&channel,command_va,3U,&put)==
           NVX_GSP_GPFIFO_BAD_COMMAND_RANGE);
    assert(nvx_gsp_gpfifo_channel_init(&channel,entries,8U,&gp_get,&gp_put,
        &doorbell,1U,4U)==NVX_GSP_GPFIFO_BAD_RING);
    assert(nvx_gsp_gpfifo_channel_init(&channel,entries,sizeof(entries),
        &gp_get,&gp_put,&doorbell,1U,3U)==NVX_GSP_GPFIFO_INVALID_ARGUMENT);

    /* Build and queue an Ampere CE copy with a GPU-written completion semaphore. */
    {
        volatile uint32_t ce_command[NVX_GSP_CE_COPY_COMMAND_DWORDS]={0};
        size_t ce_bytes=0U;
        simulated_completion_t sim;
        gp_get=0U;gp_put=0U;doorbell=0U;completion=0U;
        assert(nvx_gsp_ce_build_copy(ce_command,sizeof(ce_command),0x77U,2U,
            UINT64_C(0x20000000),UINT64_C(0x20001000),UINT64_C(0x20002000),
            4096U,0xCAFE1234U,&ce_bytes)==NVX_GSP_CE_OK);
        assert(ce_bytes==NVX_GSP_CE_COPY_COMMAND_BYTES);
        assert(nvx_gsp_gpfifo_channel_init(&channel,entries,sizeof(entries),
            &gp_get,&gp_put,&doorbell,0xBEEFU,4U)==NVX_GSP_GPFIFO_OK);
        sim=(simulated_completion_t){&gp_get,&completion,1U,0xCAFE1234U};
        assert(nvx_gsp_gpfifo_submit_and_wait(&channel,UINT64_C(0x10000000),
            ce_bytes,&completion,0xCAFE1234U,5U,1U,complete_after_poll,
            &sim,&put)==NVX_GSP_GPFIFO_OK);
        assert(put==1U&&gp_put==1U&&doorbell==0xBEEFU);
        assert(ce_command[15]==NVX_GSP_CE_LAUNCH_DMA_FLAGS);
        assert(completion==0xCAFE1234U);
    }
    return 0;
}
