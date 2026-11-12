#include "nvx_gsp_channel_runtime.h"
#include <string.h>
static nvx_gsp_rmapi_status_t bind_fifo(nvx_gsp_channel_runtime_t*r){
 if(nvx_gsp_userd_channel_init(r->userd_memory,r->userd_offset,r->userd_bytes,&r->fifo,r->entries,r->entries_mapping_bytes,r->doorbell,r->doorbell_token,r->entry_count,&r->userd)!=NVX_GSP_USERD_OK)return NVX_GSP_RMAPI_INVALID_ARGUMENT;
 r->stage=NVX_GSP_CHANNEL_RUNTIME_READY;return NVX_GSP_RMAPI_OK;
}
nvx_gsp_rmapi_status_t nvx_gsp_channel_runtime_create(nvx_gsp_rpc_manager_t*m,nvx_gsp_channel_runtime_t*r,uint32_t c,uint32_t d,uint32_t h,uint32_t va,uint32_t cid,uint32_t sub,uint64_t ring,uint32_t count,nvx_gsp_os_memory_t*u,uint64_t uoff,size_t ubytes,volatile uint32_t*entries,size_t ebytes,volatile uint32_t*door,uint32_t token,void*s,size_t ss,void*resp,size_t rs,uint32_t timeout,uint32_t poll,nvx_gsp_rmapi_delay_fn delay,void*ctx){
 nvx_gsp_userd_status_t us;nvx_gsp_rmapi_status_t st;
 if(!m||!r||r->stage||r->rm.stage||!u||!entries||!door||!s||!resp||!timeout||!poll||!delay)return NVX_GSP_RMAPI_INVALID_ARGUMENT;
 memset(r,0,sizeof(*r));us=nvx_gsp_userd_view_init(u,uoff,ubytes,&r->userd);if(us!=NVX_GSP_USERD_OK)return NVX_GSP_RMAPI_INVALID_ARGUMENT;
 us=nvx_gsp_channel_params_init_ampere(&r->params,c,d,va,cid,NVX_GSP_RM_ENGINE_TYPE_COPY0,sub,ring,count,u,uoff);if(us!=NVX_GSP_USERD_OK)return NVX_GSP_RMAPI_INVALID_ARGUMENT;
 r->userd_memory=u;r->entries=entries;r->entries_mapping_bytes=ebytes;r->doorbell=door;r->doorbell_token=token;r->entry_count=count;r->userd_offset=uoff;r->userd_bytes=ubytes;r->stage=NVX_GSP_CHANNEL_RUNTIME_CREATE_PENDING;
 st=nvx_gsp_copy_channel_create(m,&r->rm,c,d,h,&r->params,s,ss,resp,rs,timeout,poll,delay,ctx);
 if(st!=NVX_GSP_RMAPI_OK){if(r->rm.stage==NVX_GSP_CHANNEL_IDLE&&!r->rm.allocated)memset(r,0,sizeof(*r));return st;}return bind_fifo(r);
}
nvx_gsp_rmapi_status_t nvx_gsp_channel_runtime_recover(nvx_gsp_rpc_manager_t*m,nvx_gsp_channel_runtime_t*r,void*s,size_t ss,void*resp,size_t rs,uint32_t timeout,uint32_t poll,nvx_gsp_rmapi_delay_fn delay,void*ctx){
 nvx_gsp_rmapi_status_t st;if(!m||!r||!s||!resp||!timeout||!poll||!delay||(r->stage!=NVX_GSP_CHANNEL_RUNTIME_CREATE_PENDING&&r->stage!=NVX_GSP_CHANNEL_RUNTIME_DESTROY_PENDING))return NVX_GSP_RMAPI_INVALID_ARGUMENT;
 if(r->rm.stage){st=nvx_gsp_copy_channel_recover(m,&r->rm,s,ss,resp,rs,timeout,poll,delay,ctx);if(st!=NVX_GSP_RMAPI_OK)return st;}
 if(r->stage==NVX_GSP_CHANNEL_RUNTIME_DESTROY_PENDING){if(r->rm.allocated)return NVX_GSP_RMAPI_INVALID_ARGUMENT;memset(r,0,sizeof(*r));return NVX_GSP_RMAPI_OK;}
 if(!r->rm.allocated&&!r->rm.stage){memset(r,0,sizeof(*r));return NVX_GSP_RMAPI_OK;}
 if(!r->rm.allocated||!r->rm.bound||!r->rm.scheduled)return NVX_GSP_RMAPI_INVALID_ARGUMENT;return bind_fifo(r);
}
nvx_gsp_rmapi_status_t nvx_gsp_channel_runtime_stop(nvx_gsp_rpc_manager_t*m,nvx_gsp_channel_runtime_t*r,const nvx_gsp_ce_workload_t*w,void*s,size_t ss,void*resp,size_t rs,uint32_t timeout,uint32_t poll,nvx_gsp_rmapi_delay_fn delay,void*ctx){
 nvx_gsp_rmapi_status_t st;if(!m||!r||!s||!resp||!timeout||!poll||!delay||(r->stage!=NVX_GSP_CHANNEL_RUNTIME_READY&&r->stage!=NVX_GSP_CHANNEL_RUNTIME_CREATE_PENDING&&r->stage!=NVX_GSP_CHANNEL_RUNTIME_DESTROY_PENDING))return NVX_GSP_RMAPI_INVALID_ARGUMENT;
 if(w&&(w->stage!=NVX_GSP_CE_WORKLOAD_IDLE||w->pinned_count))return NVX_GSP_RMAPI_INVALID_ARGUMENT;
 if(r->stage==NVX_GSP_CHANNEL_RUNTIME_CREATE_PENDING){st=nvx_gsp_channel_runtime_recover(m,r,s,ss,resp,rs,timeout,poll,delay,ctx);if(st!=NVX_GSP_RMAPI_OK)return st;if(!r->stage)return NVX_GSP_RMAPI_OK;}
 r->stage=NVX_GSP_CHANNEL_RUNTIME_DESTROY_PENDING;st=nvx_gsp_copy_channel_destroy(m,&r->rm,s,ss,resp,rs,timeout,poll,delay,ctx);if(st!=NVX_GSP_RMAPI_OK)return st;memset(r,0,sizeof(*r));return NVX_GSP_RMAPI_OK;
}
