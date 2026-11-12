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
    /*
     * Known-answer vectors, derived independently from NVIDIA's published
     * NVC56F_GP_ENTRY0_GET (31:2), ENTRY1_GET_HI (7:0) and ENTRY1_LENGTH
     * (30:10) field definitions, NOT from the implementation's own formula.
     * VA 0x1234567800 -> ENTRY0 = VA[31:0] = 0x34567800 (bits 1:0 clear),
     * ENTRY1 = GET_HI 0x12 | (64 bytes / 4 = 16 dwords) << 10 = 0x00004012.
     */
    const uint32_t expected0=UINT32_C(0x34567800);
    const uint32_t expected1=UINT32_C(0x00004012);

    assert(nvx_gsp_gpfifo_channel_init(&channel,entries,sizeof(entries),
        &gp_get,&gp_put,&doorbell,0xBEEFU,4U)==NVX_GSP_GPFIFO_OK);
    assert(channel.initialized&&channel.put_index==0U);
    assert(nvx_gsp_gpfifo_submit(&channel,command_va,command_bytes,&put)==NVX_GSP_GPFIFO_OK);
    assert(put==1U&&gp_put==1U&&doorbell==0xBEEFU);
    assert(entries[0]==expected0&&entries[1]==expected1);
    assert(channel.submit_lock.owner_ticket ==
           channel.submit_lock.next_ticket);

    /* A second known-answer vector with every address bit pattern exercised:
     * VA 0xCD56789ABC (just under the 2^40 limit), 64 dwords ->
     * ENTRY0 0x56789ABC, ENTRY1 = GET_HI 0xCD | 64 << 10 = 0x000100CD. */
    {
        volatile uint32_t kat_entries[8]={0};
        volatile uint32_t kat_get=0U,kat_put=0U,kat_bell=0U;
        nvx_gsp_gpfifo_channel_t kat={0};
        uint32_t kat_idx=0U;
        assert(nvx_gsp_gpfifo_channel_init(&kat,kat_entries,sizeof(kat_entries),
            &kat_get,&kat_put,&kat_bell,1U,4U)==NVX_GSP_GPFIFO_OK);
        assert(nvx_gsp_gpfifo_submit(&kat,UINT64_C(0xCD56789ABC),256U,
            &kat_idx)==NVX_GSP_GPFIFO_OK);
        assert(kat_entries[0]==UINT32_C(0x56789ABC));
        assert(kat_entries[1]==UINT32_C(0x000100CD));
    }

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
    /* Queue-full failure must release the per-channel producer lock. */
    assert(channel.submit_lock.owner_ticket ==
           channel.submit_lock.next_ticket);

    /*
     * A later producer can queue work before an earlier waiter polls GET.
     * Once GET has advanced past index 1 to index 2, waiting for index 1 must
     * succeed instead of timing out on equality alone.
     */
    gp_get=0U;gp_put=0U;doorbell=0U;
    assert(nvx_gsp_gpfifo_channel_init(&channel,entries,sizeof(entries),
        &gp_get,&gp_put,&doorbell,0xBEEFU,4U)==NVX_GSP_GPFIFO_OK);
    assert(nvx_gsp_gpfifo_submit(&channel,command_va,command_bytes,&put)==
           NVX_GSP_GPFIFO_OK && put==1U);
    gp_get=1U;
    assert(nvx_gsp_gpfifo_submit(&channel,command_va,command_bytes,&put)==
           NVX_GSP_GPFIFO_OK && put==2U);
    gp_get=2U;
    assert(nvx_gsp_gpfifo_wait_get(&channel,1U,5U,1U,no_delay,NULL)==
           NVX_GSP_GPFIFO_OK);

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
        assert(nvx_gsp_ce_build_copy(ce_command,sizeof(ce_command),
            NVX_GSP_CE_GA10X_COPY_CLASS,2U,
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
