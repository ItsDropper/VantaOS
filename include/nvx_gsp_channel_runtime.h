#ifndef VANTA_NVX_GSP_CHANNEL_RUNTIME_H
#define VANTA_NVX_GSP_CHANNEL_RUNTIME_H
#include "nvx_gsp_ce_workload.h"
#include "nvx_gsp_channel_ring.h"
/* USERD and ring mappings are borrowed and must outlive this runtime. */
typedef enum nvx_gsp_channel_runtime_stage { NVX_GSP_CHANNEL_RUNTIME_IDLE=0, NVX_GSP_CHANNEL_RUNTIME_CREATE_PENDING, NVX_GSP_CHANNEL_RUNTIME_READY, NVX_GSP_CHANNEL_RUNTIME_DESTROY_PENDING } nvx_gsp_channel_runtime_stage_t;
typedef struct nvx_gsp_channel_runtime {
 nvx_gsp_copy_channel_t rm;
 nvx_gsp_os_memory_t *userd_memory;
 nvx_gsp_userd_view_t userd;
 nvx_gsp_gpfifo_channel_t fifo;
 nvx_gsp_channel_alloc_params_t params;
 volatile uint32_t *entries,*doorbell;
 size_t entries_mapping_bytes,userd_bytes;
 uint32_t doorbell_token,entry_count;
 uint64_t userd_offset;
 uint8_t stage;
 uint8_t userd_reference_held;
 nvx_gsp_channel_ring_t *ring;
 uint8_t ring_reference_held;
} nvx_gsp_channel_runtime_t;
/* Legacy path for caller-owned, already-mapped ring storage. */
nvx_gsp_rmapi_status_t nvx_gsp_channel_runtime_create(nvx_gsp_rpc_manager_t*,nvx_gsp_channel_runtime_t*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint64_t,uint32_t,nvx_gsp_os_memory_t*,uint64_t,size_t,volatile uint32_t*,size_t,volatile uint32_t*,uint32_t,void*,size_t,void*,size_t,uint32_t,uint32_t,nvx_gsp_rmapi_delay_fn,void*);

/* Managed path: holds a reference to an RM-backed ring until channel free. */
nvx_gsp_rmapi_status_t nvx_gsp_channel_runtime_create_managed(
    nvx_gsp_rpc_manager_t *, nvx_gsp_channel_runtime_t *,
    uint32_t client, uint32_t device, uint32_t channel, uint32_t vaspace,
    uint32_t channel_id, uint32_t subdevice_mask,
    nvx_gsp_channel_ring_t *ring, nvx_gsp_os_memory_t *userd_memory,
    uint64_t userd_offset, size_t userd_bytes,
    volatile uint32_t *doorbell, uint32_t doorbell_token,
    void *scratch, size_t scratch_size, void *response, size_t response_size,
    uint32_t timeout_ms, uint32_t poll_ms,
    nvx_gsp_rmapi_delay_fn delay_ms, void *delay_context);

nvx_gsp_rmapi_status_t nvx_gsp_channel_runtime_recover(nvx_gsp_rpc_manager_t*,nvx_gsp_channel_runtime_t*,void*,size_t,void*,size_t,uint32_t,uint32_t,nvx_gsp_rmapi_delay_fn,void*);
/* Teardown is refused while the supplied CE workload retains pins/in-flight work. */
nvx_gsp_rmapi_status_t nvx_gsp_channel_runtime_stop(nvx_gsp_rpc_manager_t*,nvx_gsp_channel_runtime_t*,const nvx_gsp_ce_workload_t*,void*,size_t,void*,size_t,uint32_t,uint32_t,nvx_gsp_rmapi_delay_fn,void*);
#endif
