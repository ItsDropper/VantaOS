/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_rpc.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define PAGES 40U
#define BYTES (PAGES*NVX_GSP_PAGE_SIZE)
static uint8_t tx_store[BYTES] __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
static uint8_t rx_store[BYTES] __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
static uint8_t scratch[16U*NVX_GSP_PAGE_SIZE] __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
typedef struct fake_io {uint32_t regs[8];} fake_io_t;
static int rd(void *p,uint32_t r,uint32_t *o){fake_io_t*f=p;if(r/4U>=8U)return 0;*o=f->regs[r/4U];return 1;}
static int wr(void *p,uint32_t r,uint32_t v){fake_io_t*f=p;if(r/4U>=8U)return 0;f->regs[r/4U]=v;return 1;}
static void barrier(void *p){(void)p;__atomic_thread_fence(__ATOMIC_SEQ_CST);}
static void put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8U);p[2]=(uint8_t)(v>>16U);p[3]=(uint8_t)(v>>24U);}
static void reply(uint8_t *slot,uint32_t function,uint32_t sequence,uint32_t result,
                  const void *payload,size_t payload_size)
{
 size_t length=NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE+payload_size;
 memset(slot,0,NVX_GSP_PAGE_SIZE);
 put32(slot,NVX_GSP_MCTP_MAGIC);put32(slot+4U,(uint32_t)length);
 put32(slot+8U,NVX_GSP_MCTP_HEADER_SINGLE);put32(slot+12U,NVX_GSP_NVDM_HEADER_RM_RPC);
 put32(slot+16U,(uint32_t)(NVX_GSP_RPC_HEADER_SIZE+payload_size));
 put32(slot+24U,NVX_GSP_RPC_HEADER_VERSION_R535);put32(slot+28U,NVX_GSP_RPC_SIGNATURE_R535);
 put32(slot+32U,(uint32_t)(NVX_GSP_RPC_HEADER_SIZE+payload_size));put32(slot+36U,function);
 put32(slot+40U,result);put32(slot+44U,0U);put32(slot+48U,sequence);
 if(payload_size)memcpy(slot+NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE,payload,payload_size);
}
int main(void)
{
 fake_io_t f={0};nvx_gsp_msgq_io_t io={rd,wr,barrier,barrier,&f};
 nvx_gsp_msgq_queue_t tx={0},rx={0};nvx_gsp_msgq_rpc_transport_t transport={0};
 nvx_gsp_rpc_manager_t manager={0};nvx_gsp_msgq_layout_t header={0};
 nvx_gsp_rpc_handle_t a={0},b={0},c={0},d={0},e={0},lost={0};
 nvx_gsp_rpc_result_t result;nvx_gsp_rpc_view_t unmatched;
 char response_a[16]={0},response_b[16]={0},response_c[4]={0},small[4]={0};
 const char req[]="req",a_data[]="A",b_data[]="B",fail_data[]="failed",large_data[]="12345678";
 assert(nvx_gsp_msgq_queue_init_tx(&tx,tx_store,sizeof(tx_store),4096U,&io,0U,4U)==NVX_GSP_MSGQ_QUEUE_OK);
 assert(nvx_gsp_msgq_prepare_tx(rx_store,sizeof(rx_store),4096U,&header)==NVX_GSP_MSGQ_OK);
 assert(nvx_gsp_msgq_queue_init_rx(&rx,rx_store,sizeof(rx_store),4096U,&io,8U,12U)==NVX_GSP_MSGQ_QUEUE_OK);
 assert(nvx_gsp_msgq_rpc_init(&transport,&tx,&rx)==NVX_GSP_MSGQ_RPC_OK);
 assert(nvx_gsp_rpc_init_msgq(&manager,&transport)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_init_msgq(&manager,&transport)==NVX_GSP_RPC_INVALID_ARGUMENT);
 assert(nvx_gsp_rpc_submit(&manager,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,
     10U,20U,req,sizeof(req),scratch,sizeof(scratch),response_a,sizeof(response_a),&a)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_submit(&manager,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,
     11U,21U,req,sizeof(req),scratch,sizeof(scratch),response_b,sizeof(response_b),&b)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_query(&manager,&a,&result)==NVX_GSP_RPC_PENDING);
 /* Replies deliberately arrive in reverse request order. */
 reply(rx_store+NVX_GSP_PAGE_SIZE,21U,2U,0U,b_data,sizeof(b_data));
 reply(rx_store+2U*NVX_GSP_PAGE_SIZE,20U,1U,0U,a_data,sizeof(a_data));
 f.regs[2]=2U;
 assert(nvx_gsp_rpc_pump(&manager,scratch,sizeof(scratch),&unmatched)==NVX_GSP_RPC_COMPLETE);
 assert(nvx_gsp_rpc_query(&manager,&b,&result)==NVX_GSP_RPC_COMPLETE);
 assert(result.sequence==2U&&result.response_size==sizeof(b_data)&&memcmp(response_b,b_data,sizeof(b_data))==0);
 assert(nvx_gsp_rpc_query(&manager,&a,&result)==NVX_GSP_RPC_PENDING);
 assert(nvx_gsp_rpc_pump(&manager,scratch,sizeof(scratch),&unmatched)==NVX_GSP_RPC_COMPLETE);
 assert(nvx_gsp_rpc_query(&manager,&a,&result)==NVX_GSP_RPC_COMPLETE);
 assert(result.sequence==1U&&memcmp(response_a,a_data,sizeof(a_data))==0);
 assert(nvx_gsp_rpc_release(&manager,&a)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_release(&manager,&b)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_submit(&manager,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,
     12U,22U,req,sizeof(req),scratch,sizeof(scratch),response_c,sizeof(response_c),&c)==NVX_GSP_RPC_OK);
 reply(rx_store+3U*NVX_GSP_PAGE_SIZE,22U,3U,0x1234U,fail_data,sizeof(fail_data));
 f.regs[2]=3U;
 assert(nvx_gsp_rpc_pump(&manager,scratch,sizeof(scratch),&unmatched)==NVX_GSP_RPC_FAILED);
 assert(nvx_gsp_rpc_query(&manager,&c,&result)==NVX_GSP_RPC_FAILED&&result.firmware_result==0x1234U);
 assert(nvx_gsp_rpc_release(&manager,&c)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_submit(&manager,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,
     13U,23U,req,sizeof(req),scratch,sizeof(scratch),response_c,sizeof(response_c),&d)==NVX_GSP_RPC_OK);
 reply(rx_store+4U*NVX_GSP_PAGE_SIZE,999U,4U,0U,a_data,sizeof(a_data));
 f.regs[2]=4U;
 assert(nvx_gsp_rpc_pump(&manager,scratch,sizeof(scratch),&unmatched)==NVX_GSP_RPC_FUNCTION_MISMATCH);
 assert(nvx_gsp_rpc_query(&manager,&d,&result)==NVX_GSP_RPC_FUNCTION_MISMATCH);
 assert(nvx_gsp_rpc_release(&manager,&d)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_submit(&manager,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,
     14U,24U,req,sizeof(req),scratch,sizeof(scratch),small,sizeof(small),&e)==NVX_GSP_RPC_OK);
 reply(rx_store+5U*NVX_GSP_PAGE_SIZE,24U,5U,0U,large_data,sizeof(large_data));
 f.regs[2]=5U;
 assert(nvx_gsp_rpc_pump(&manager,scratch,sizeof(scratch),&unmatched)==NVX_GSP_RPC_RESPONSE_TOO_LARGE);
 assert(nvx_gsp_rpc_release(&manager,&e)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_submit(&manager,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,
     15U,25U,req,sizeof(req),scratch,sizeof(scratch),response_a,sizeof(response_a),&lost)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_mark_device_lost(&manager)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_query(&manager,&lost,&result)==NVX_GSP_RPC_DEVICE_LOST);
 assert(nvx_gsp_rpc_submit(&manager,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,
     16U,26U,req,sizeof(req),scratch,sizeof(scratch),response_a,sizeof(response_a),&a)==NVX_GSP_RPC_DEVICE_LOST);
 return 0;
}
