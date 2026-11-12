/*
 * Copyright (c) 2026 ItsDropper
 * Licensed under the MIT License; see LICENSE.
 */
#include "nvx_gsp_os_memory.h"
#include "gpu_dma.h"
#include <assert.h>
#include <string.h>

static nvx_gsp_os_memory_params_t captured;
static uint32_t c_client, c_parent, c_object, c_class;
static nvx_gsp_rmapi_status_t wait_status = NVX_GSP_RMAPI_OK;
static unsigned alloc_calls, free_calls, release_calls, dma_free_calls;
static uint64_t next_phys = 0x02000000U;

nvx_dma_status_t nvx_dma_alloc_identity_mapped(size_t size, nvx_dma_buffer_t *b)
{
    memset(b, 0, sizeof(*b));
    b->cpu_address = (void *)(uintptr_t)next_phys;
    b->gpu_address = next_phys;
    b->size = size;
    b->allocated_size = (size + 4095U) & ~(size_t)4095U;
    b->page_count = b->allocated_size / 4096U;
    b->address_bits = 30U;
    b->acquired = 1U;
    next_phys += b->allocated_size;
    return NVX_DMA_OK;
}
void nvx_dma_release(nvx_dma_buffer_t *b)
{
    ++dma_free_calls;
    memset(b, 0, sizeof(*b));
}
int gpu_dma_buffer_is_valid(const gpu_dma_buffer_t *b)
{
    return b && b->acquired && b->cpu_address &&
        b->gpu_address == (uint64_t)(uintptr_t)b->cpu_address &&
        b->allocated_size >= b->size &&
        b->allocated_size == b->page_count * 4096U;
}
nvx_gsp_rmapi_status_t nvx_gsp_rm_alloc_submit(
    nvx_gsp_rpc_manager_t *mgr, uint32_t client, uint32_t parent,
    uint32_t object, uint32_t cls, uint32_t flags, const void *params,
    size_t params_size, void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, nvx_gsp_rmapi_result_t *out)
{
    (void)mgr; (void)flags; (void)scratch; (void)scratch_size;
    (void)response; (void)response_capacity;
    assert(params_size == sizeof(captured));
    memcpy(&captured, params, sizeof(captured));
    c_client=client; c_parent=parent; c_object=object; c_class=cls;
    ++alloc_calls;
    out->handle.valid=1U; out->function=NVX_GSP_RPC_FN_RM_ALLOC;
    return NVX_GSP_RMAPI_OK;
}
nvx_gsp_rmapi_status_t nvx_gsp_rm_free_submit(
    nvx_gsp_rpc_manager_t *mgr, uint32_t root, uint32_t parent,
    uint32_t object, void *scratch, size_t scratch_size, void *response,
    size_t response_capacity, nvx_gsp_rmapi_result_t *out)
{
    (void)mgr; (void)scratch; (void)scratch_size;
    (void)response; (void)response_capacity;
    assert(root==c_client && parent==c_parent && object==c_object);
    ++free_calls;
    out->handle.valid=1U; out->function=NVX_GSP_RPC_FN_FREE;
    return NVX_GSP_RMAPI_OK;
}
nvx_gsp_rmapi_status_t nvx_gsp_rmapi_wait(
    nvx_gsp_rpc_manager_t *mgr, nvx_gsp_rmapi_result_t *op,
    void *scratch, size_t scratch_size, void *response, size_t response_capacity,
    uint32_t timeout, uint32_t interval, nvx_gsp_rmapi_delay_fn delay,
    void *context)
{
    (void)mgr; (void)op; (void)scratch; (void)scratch_size;
    (void)response; (void)response_capacity; (void)timeout;
    (void)interval; (void)delay; (void)context;
    return wait_status;
}
nvx_gsp_rpc_status_t nvx_gsp_rpc_release(
    nvx_gsp_rpc_manager_t *mgr, nvx_gsp_rpc_handle_t *h)
{
    (void)mgr;
    assert(h && h->valid);
    ++release_calls; h->valid=0U;
    return NVX_GSP_RPC_OK;
}
static void delay(uint32_t ms, void *ctx) { (void)ms; (void)ctx; }

int main(void)
{
    nvx_gsp_rpc_manager_t manager={0};
    nvx_gsp_os_memory_t m={0};
    uint8_t scratch[128], response[128];
    void *cpu=NULL;
    uint64_t phys=0, bytes=0;

    assert(nvx_gsp_os_memory_create(&manager,&m,1,2,3,9000,0x1234,
        scratch,sizeof(scratch),response,sizeof(response),100,1,delay,NULL)
        ==NVX_GSP_RMAPI_OK);
    assert(alloc_calls==1 && m.stage==NVX_GSP_OS_MEMORY_CREATED);
    assert(c_client==1 && c_parent==2 && c_object==3 &&
        c_class==NVX_GSP_RM_CLASS_MEMORY_SYSTEM_OS_DESCRIPTOR);
    assert(captured.type==NVX_GSP_OS_MEMORY_TYPE_IMAGE && captured.flags==0);
    assert(captured.attr==NVX_GSP_OS_MEMORY_ATTR_SYSTEM_CONTIGUOUS_CACHED &&
        captured.attr2==NVX_GSP_OS_MEMORY_ATTR2_GPU_CACHEABLE_NO);
    assert(captured.descriptor==m.backing.gpu_address &&
        captured.limit==12287 && captured.descriptor_type==
        NVX_GSP_OS_MEMORY_DESCRIPTOR_OS_PHYS_ADDR && captured.tag==0x1234);
    assert(nvx_gsp_os_memory_get_cpu_address(&m,&cpu)==NVX_GSP_RMAPI_OK &&
        cpu==m.backing.cpu_address);
    assert(nvx_gsp_os_memory_get_physical_range(&m,&phys,&bytes)==
        NVX_GSP_RMAPI_OK && phys==captured.descriptor && bytes==12288);

    assert(nvx_gsp_os_memory_acquire(&m) == NVX_GSP_RMAPI_OK);
    assert(m.lifetime_state == 1U);
    assert(nvx_gsp_os_memory_destroy(&manager,&m,scratch,sizeof(scratch),
        response,sizeof(response),100,1,delay,NULL) == NVX_GSP_RMAPI_BUSY);
    assert(m.stage == NVX_GSP_OS_MEMORY_CREATED && free_calls == 0U);
    assert(nvx_gsp_os_memory_release(&m) == NVX_GSP_RMAPI_OK);
    assert(m.lifetime_state == 0U);

    wait_status=NVX_GSP_RMAPI_TIMEOUT;
    assert(nvx_gsp_os_memory_destroy(&manager,&m,scratch,sizeof(scratch),
        response,sizeof(response),100,1,delay,NULL)==NVX_GSP_RMAPI_TIMEOUT);
    assert(m.stage==NVX_GSP_OS_MEMORY_FREE_PENDING && m.backing.acquired &&
        dma_free_calls==0);
    wait_status=NVX_GSP_RMAPI_OK;
    assert(nvx_gsp_os_memory_recover(&manager,&m,scratch,sizeof(scratch),
        response,sizeof(response),100,1,delay,NULL)==NVX_GSP_RMAPI_OK);
    assert(m.stage==NVX_GSP_OS_MEMORY_IDLE && !m.backing.acquired &&
        dma_free_calls==1 && free_calls==1 && release_calls==2);

    memset(&m,0,sizeof(m));
    wait_status=NVX_GSP_RMAPI_TIMEOUT;
    assert(nvx_gsp_os_memory_create(&manager,&m,1,2,4,4096,7,
        scratch,sizeof(scratch),response,sizeof(response),100,1,delay,NULL)
        ==NVX_GSP_RMAPI_TIMEOUT);
    wait_status=NVX_GSP_RMAPI_MALFORMED_RESPONSE;
    assert(nvx_gsp_os_memory_recover(&manager,&m,scratch,sizeof(scratch),
        response,sizeof(response),100,1,delay,NULL)==
        NVX_GSP_RMAPI_MALFORMED_RESPONSE);
    assert(m.stage==NVX_GSP_OS_MEMORY_ALLOC_QUARANTINED &&
        m.backing.acquired && m.operation.handle.valid && dma_free_calls==1);
    wait_status=NVX_GSP_RMAPI_REMOTE_ERROR;
    assert(nvx_gsp_os_memory_recover(&manager,&m,scratch,sizeof(scratch),
        response,sizeof(response),100,1,delay,NULL)==NVX_GSP_RMAPI_REMOTE_ERROR);
    assert(m.stage==NVX_GSP_OS_MEMORY_IDLE && !m.backing.acquired &&
        dma_free_calls==2);

    memset(&m,0,sizeof(m));
    wait_status=NVX_GSP_RMAPI_OK;
    assert(nvx_gsp_os_memory_create(&manager,&m,1,2,5,4096,0,
        scratch,sizeof(scratch),response,sizeof(response),100,1,delay,NULL)
        ==NVX_GSP_RMAPI_OK);
    wait_status=NVX_GSP_RMAPI_MALFORMED_RESPONSE;
    assert(nvx_gsp_os_memory_destroy(&manager,&m,scratch,sizeof(scratch),
        response,sizeof(response),100,1,delay,NULL)==
        NVX_GSP_RMAPI_MALFORMED_RESPONSE);
    assert(m.stage==NVX_GSP_OS_MEMORY_FREE_QUARANTINED &&
        m.backing.acquired && dma_free_calls==2);
    return 0;
}
