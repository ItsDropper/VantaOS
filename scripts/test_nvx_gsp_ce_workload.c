/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_ce_workload.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint8_t cpu_backing[4][4096] __attribute__((aligned(4096)));
static dma_page_t fake_pages[4];
static nvx_dma_buffer_t dma_buffers[4];
static nvx_memory_info_t memory_info[4];
static unsigned pin_count[4];

static int handle_index(const nvx_memory_handle_t *h)
{
    return h && h->slot < 4U && h->token == (uint64_t)h->slot + 1U ? 
        (int)h->slot : -1;
}

nvx_memory_status_t nvx_memory_get_info(nvx_memory_manager_t *m,
    const nvx_memory_handle_t *h, nvx_memory_info_t *out)
{
    int i=handle_index(h); (void)m;
    if (out) *out=(nvx_memory_info_t){0};
    if (!out || i<0) return NVX_MEMORY_STALE_HANDLE;
    *out=memory_info[i];
    return NVX_MEMORY_OK;
}

nvx_memory_status_t nvx_memory_pin(nvx_memory_manager_t *m,
    const nvx_memory_handle_t *h)
{
    int i=handle_index(h); (void)m;
    if(i<0) return NVX_MEMORY_STALE_HANDLE;
    ++pin_count[i];
    return NVX_MEMORY_OK;
}

nvx_memory_status_t nvx_memory_unpin(nvx_memory_manager_t *m,
    const nvx_memory_handle_t *h)
{
    int i=handle_index(h); (void)m;
    if(i<0 || !pin_count[i]) return NVX_MEMORY_STALE_HANDLE;
    --pin_count[i];
    return NVX_MEMORY_OK;
}

nvx_memory_status_t nvx_memory_get_dma_buffer(nvx_memory_manager_t *m,
    const nvx_memory_handle_t *h, nvx_dma_buffer_t **out)
{
    int i=handle_index(h); (void)m;
    if(out) *out=0;
    if(i<0 || !out || !pin_count[i]) return NVX_MEMORY_BUSY;
    *out=&dma_buffers[i];
    return NVX_MEMORY_OK;
}

typedef struct gpu_sim {
    volatile uint32_t *gp_get;
    volatile uint32_t *gp_put;
    volatile uint32_t *completion;
    uint32_t completion_value;
} gpu_sim_t;

static void complete_gpu(uint32_t ms, void *context)
{
    gpu_sim_t *gpu=(gpu_sim_t *)context;
    (void)ms;
    __atomic_store_n(gpu->gp_get,
        __atomic_load_n(gpu->gp_put,__ATOMIC_ACQUIRE),__ATOMIC_RELEASE);
    __atomic_store_n(gpu->completion,gpu->completion_value,__ATOMIC_RELEASE);
}

static void no_progress(uint32_t ms, void *context)
{
    (void)ms; (void)context;
}

static void setup_memory(nvx_memory_manager_t *manager,
                         nvx_memory_handle_t handles[4])
{
    memset(manager,0,sizeof(*manager));
    memset(pin_count,0,sizeof(pin_count));
    for(unsigned i=0;i<4U;i++) {
        handles[i]=(nvx_memory_handle_t){
            .token=(uint64_t)i+1U,
            .gpu_virtual_address=UINT64_C(0x10000000)+(uint64_t)i*0x10000U,
            .size=sizeof(cpu_backing[i]),
            .slot=i,
            .generation=1U
        };
        dma_buffers[i]=(nvx_dma_buffer_t){
            .pages=&fake_pages[i],
            .cpu_address=cpu_backing[i],
            .gpu_address=handles[i].gpu_virtual_address,
            .size=sizeof(cpu_backing[i]),
            .allocated_size=sizeof(cpu_backing[i]),
            .page_count=1U,
            .allocation_id=(uint64_t)i+1U,
            .address_bits=40U,
            .acquired=1U
        };
        memory_info[i]=(nvx_memory_info_t){
            .cpu_address=cpu_backing[i],
            .gpu_virtual_address=handles[i].gpu_virtual_address,
            .size=sizeof(cpu_backing[i]),
            .kind=i==0U?NVX_MEMORY_COMMAND_STORAGE:NVX_MEMORY_STAGING,
            .access=i==0U||i==1U?NVX_MEMORY_ACCESS_READ:
                NVX_MEMORY_ACCESS_READ|NVX_MEMORY_ACCESS_WRITE
        };
    }
}

static void init_channel(nvx_gsp_gpfifo_channel_t *channel,
    volatile uint32_t *entries,volatile uint32_t *get,
    volatile uint32_t *put,volatile uint32_t *doorbell,uint32_t count)
{
    assert(nvx_gsp_gpfifo_channel_init(channel,entries,
        count*2U*sizeof(uint32_t),get,put,doorbell,0xA55AU,count)==
        NVX_GSP_GPFIFO_OK);
}

int main(void)
{
    nvx_memory_manager_t manager;
    nvx_memory_handle_t handles[4];
    nvx_gsp_ce_workload_t workload={0};
    nvx_gsp_gpfifo_channel_t channel={0};
    volatile uint32_t entries[8]={0};
    volatile uint32_t get=0U,put=0U,doorbell=0U;
    nvx_gsp_gpfifo_status_t fifo_status=NVX_GSP_GPFIFO_INVALID_ARGUMENT;
    gpu_sim_t gpu={&get,&put,(volatile uint32_t *)cpu_backing[3],0xCAFE1234U};

    setup_memory(&manager,handles);
    init_channel(&channel,entries,&get,&put,&doorbell,4U);
    assert(nvx_gsp_ce_copy_start(&workload,&manager,&channel,
        &handles[0],&handles[1],&handles[2],&handles[3],
        NVX_GSP_CE_GA10X_COPY_CLASS,2U,256U,gpu.completion_value,
        5U,1U,complete_gpu,&gpu,&fifo_status)==NVX_GSP_CE_WORKLOAD_OK);
    assert(fifo_status==NVX_GSP_GPFIFO_OK);
    assert(workload.stage==NVX_GSP_CE_WORKLOAD_IDLE);
    for(unsigned i=0;i<4U;i++) assert(pin_count[i]==0U);
    assert(*(volatile uint32_t *)cpu_backing[3]==gpu.completion_value);

    /* A timeout after queue publication retains all four pins and DMA ownership. */
    memset(&workload,0,sizeof(workload));
    get=0U;put=0U;doorbell=0U;
    init_channel(&channel,entries,&get,&put,&doorbell,4U);
    assert(nvx_gsp_ce_copy_start(&workload,&manager,&channel,
        &handles[0],&handles[1],&handles[2],&handles[3],
        NVX_GSP_CE_GA10X_COPY_CLASS,2U,256U,0x11223344U,
        2U,1U,no_progress,NULL,&fifo_status)==NVX_GSP_CE_WORKLOAD_IN_FLIGHT);
    assert(workload.stage==NVX_GSP_CE_WORKLOAD_PENDING);
    assert(workload.put_index==1U&&put==1U);
    for(unsigned i=0;i<4U;i++) assert(pin_count[i]==1U);
    assert(workload.sync[0].owner==NVX_DMA_OWNER_DEVICE);
    assert(workload.sync[3].owner==NVX_DMA_OWNER_SHARED);
    gpu.completion_value=0x11223344U;
    assert(nvx_gsp_ce_copy_recover(&workload,5U,1U,complete_gpu,&gpu)==
        NVX_GSP_CE_WORKLOAD_OK);
    assert(workload.stage==NVX_GSP_CE_WORKLOAD_IDLE);
    for(unsigned i=0;i<4U;i++) assert(pin_count[i]==0U);

    /* Queue-full failure happens before publication and rolls every pin back. */
    memset(&workload,0,sizeof(workload));
    get=1U;put=0U;doorbell=0U;
    init_channel(&channel,entries,&get,&put,&doorbell,2U);
    assert(nvx_gsp_ce_copy_start(&workload,&manager,&channel,
        &handles[0],&handles[1],&handles[2],&handles[3],
        NVX_GSP_CE_GA10X_COPY_CLASS,2U,256U,0x55667788U,
        2U,1U,no_progress,NULL,&fifo_status)==NVX_GSP_CE_WORKLOAD_SUBMIT_ERROR);
    assert(fifo_status==NVX_GSP_GPFIFO_QUEUE_FULL);
    for(unsigned i=0;i<4U;i++) assert(pin_count[i]==0U);

    /* Wrong access permissions and aliased resource handles are rejected early. */
    memory_info[2].access=NVX_MEMORY_ACCESS_READ;
    assert(nvx_gsp_ce_copy_start(&workload,&manager,&channel,
        &handles[0],&handles[1],&handles[2],&handles[3],
        NVX_GSP_CE_GA10X_COPY_CLASS,2U,256U,1U,
        2U,1U,no_progress,NULL,&fifo_status)==
        NVX_GSP_CE_WORKLOAD_INVALID_ARGUMENT);
    memory_info[2].access=NVX_MEMORY_ACCESS_READ|NVX_MEMORY_ACCESS_WRITE;
    assert(nvx_gsp_ce_copy_start(&workload,&manager,&channel,
        &handles[0],&handles[1],&handles[1],&handles[3],
        NVX_GSP_CE_GA10X_COPY_CLASS,2U,256U,1U,
        2U,1U,no_progress,NULL,&fifo_status)==
        NVX_GSP_CE_WORKLOAD_INVALID_ARGUMENT);
    for(unsigned i=0;i<4U;i++) assert(pin_count[i]==0U);
    return 0;
}
