/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_ce.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct simulated_gpu {
    volatile uint32_t *gp_get;
    volatile uint32_t *completion;
    uint32_t consumed_index;
    uint32_t completion_value;
} simulated_gpu_t;

static void complete_copy(uint32_t milliseconds,void *context)
{
    simulated_gpu_t *gpu=(simulated_gpu_t *)context;
    (void)milliseconds;
    __atomic_store_n(gpu->gp_get,gpu->consumed_index,__ATOMIC_RELEASE);
    __atomic_store_n(gpu->completion,gpu->completion_value,__ATOMIC_RELEASE);
}

static void no_progress(uint32_t milliseconds,void *context)
{
    (void)milliseconds;
    (void)context;
}

static void init_channel(nvx_gsp_gpfifo_channel_t *channel,
    volatile uint32_t *entries,volatile uint32_t *get,
    volatile uint32_t *put,volatile uint32_t *doorbell)
{
    assert(nvx_gsp_gpfifo_channel_init(channel,entries,8U*sizeof(uint32_t),
        get,put,doorbell,0xA55AU,4U)==NVX_GSP_GPFIFO_OK);
}

int main(void)
{
    volatile uint32_t entries[8]={0};
    volatile uint32_t command[NVX_GSP_CE_COPY_COMMAND_DWORDS]={0};
    volatile uint32_t get=0U,put=0U,doorbell=0U,completion=0x11111111U;
    nvx_gsp_gpfifo_channel_t channel={0};
    nvx_gsp_gpfifo_status_t fifo_status=NVX_GSP_GPFIFO_BAD_RING;
    size_t command_bytes=SIZE_MAX;
    uint32_t submitted_put=UINT32_MAX;
    simulated_gpu_t gpu={&get,&completion,1U,0xCAFE1234U};
    const uint64_t command_va=UINT64_C(0x10000000);
    const uint64_t source_va=UINT64_C(0x20000000);
    const uint64_t destination_va=UINT64_C(0x20001000);
    const uint64_t completion_va=UINT64_C(0x20002000);

    init_channel(&channel,entries,&get,&put,&doorbell);
    assert(nvx_gsp_ce_copy_submit_and_wait(&channel,command,
        sizeof(command),command_va,NVX_GSP_CE_GA10X_COPY_CLASS,2U,
        source_va,destination_va,completion_va,&completion,4096U,
        gpu.completion_value,10U,1U,complete_copy,&gpu,&command_bytes,
        &submitted_put,&fifo_status)==NVX_GSP_CE_RUNTIME_OK);
    assert(command_bytes==NVX_GSP_CE_COPY_COMMAND_BYTES);
    assert(submitted_put==1U&&put==1U&&get==1U);
    assert(doorbell==0xA55AU&&completion==gpu.completion_value);
    assert(fifo_status==NVX_GSP_GPFIFO_OK);
    assert(command[1]==NVX_GSP_CE_GA10X_COPY_CLASS);
    assert(command[15]==NVX_GSP_CE_LAUNCH_DMA_FLAGS);

    /* A reused semaphore value must not let a new submission pass instantly. */
    command_bytes=SIZE_MAX;
    submitted_put=UINT32_MAX;
    fifo_status=NVX_GSP_GPFIFO_BAD_RING;
    assert(nvx_gsp_ce_copy_submit_and_wait(&channel,command,sizeof(command),
        command_va,NVX_GSP_CE_GA10X_COPY_CLASS,2U,source_va,destination_va,
        completion_va,&completion,4096U,gpu.completion_value,10U,1U,
        complete_copy,&gpu,&command_bytes,&submitted_put,&fifo_status)==
        NVX_GSP_CE_RUNTIME_STALE_COMPLETION);
    assert(command_bytes==0U&&submitted_put==0U);
    assert(put==1U&&fifo_status==NVX_GSP_GPFIFO_INVALID_ARGUMENT);

    /* Invalid copy ranges fail before the queue is modified. */
    completion=0x11111111U;
    assert(nvx_gsp_ce_copy_submit_and_wait(&channel,command,sizeof(command),
        command_va,NVX_GSP_CE_GA10X_COPY_CLASS,2U,source_va,destination_va,
        completion_va,&completion,0U,gpu.completion_value,10U,1U,
        complete_copy,&gpu,&command_bytes,&submitted_put,&fifo_status)==
        NVX_GSP_CE_RUNTIME_BUILD_FAILED);
    assert(command_bytes==0U&&submitted_put==0U&&put==1U);

    /* A fetch/completion timeout is not cancellation: retain the put index. */
    get=0U;put=0U;doorbell=0U;completion=0x11111111U;
    init_channel(&channel,entries,&get,&put,&doorbell);
    assert(nvx_gsp_ce_copy_submit_and_wait(&channel,command,sizeof(command),
        command_va,NVX_GSP_CE_GA10X_COPY_CLASS,2U,source_va,destination_va,
        completion_va,&completion,4096U,gpu.completion_value,2U,1U,
        no_progress,NULL,&command_bytes,&submitted_put,&fifo_status)==
        NVX_GSP_CE_RUNTIME_TIMEOUT);
    assert(command_bytes==NVX_GSP_CE_COPY_COMMAND_BYTES);
    assert(submitted_put==1U&&put==1U&&doorbell==0xA55AU);
    assert(fifo_status==NVX_GSP_GPFIFO_TIMEOUT);

    /* A full ring fails closed without publishing a new queue entry. */
    get=1U;put=0U;doorbell=0U;completion=0x11111111U;
    init_channel(&channel,entries,&get,&put,&doorbell);
    assert(nvx_gsp_ce_copy_submit_and_wait(&channel,command,sizeof(command),
        command_va,NVX_GSP_CE_GA10X_COPY_CLASS,2U,source_va,destination_va,
        completion_va,&completion,4096U,gpu.completion_value,2U,1U,
        no_progress,NULL,&command_bytes,&submitted_put,&fifo_status)==
        NVX_GSP_CE_RUNTIME_SUBMIT_FAILED);
    assert(command_bytes==NVX_GSP_CE_COPY_COMMAND_BYTES);
    assert(submitted_put==0U&&put==0U&&doorbell==0U);
    assert(fifo_status==NVX_GSP_GPFIFO_QUEUE_FULL);

    /* CPU command and completion mappings must be independent. */
    assert(nvx_gsp_ce_copy_submit_and_wait(&channel,command,sizeof(command),
        command_va,NVX_GSP_CE_GA10X_COPY_CLASS,2U,source_va,destination_va,
        completion_va,command,4096U,gpu.completion_value,2U,1U,
        no_progress,NULL,&command_bytes,&submitted_put,&fifo_status)==
        NVX_GSP_CE_RUNTIME_INVALID_ARGUMENT);

    /* Output objects may not alias one another. */
    assert(nvx_gsp_ce_copy_submit_and_wait(&channel,command,sizeof(command),
        command_va,NVX_GSP_CE_GA10X_COPY_CLASS,2U,source_va,destination_va,
        completion_va,&completion,4096U,gpu.completion_value,2U,1U,
        no_progress,NULL,&command_bytes,
        (uint32_t *)(void *)&command_bytes,&fifo_status)==
        NVX_GSP_CE_RUNTIME_INVALID_ARGUMENT);

    return 0;
}
