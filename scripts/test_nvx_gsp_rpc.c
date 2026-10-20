/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_rpc.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define PAGES 63U
static uint8_t tx_mem[PAGES*NVX_GSP_PAGE_SIZE] __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
static uint8_t rx_mem[PAGES*NVX_GSP_PAGE_SIZE] __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
static uint8_t scratch[16U*NVX_GSP_PAGE_SIZE] __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
static uint8_t large_request[100000U];
static volatile uint32_t tx_r,tx_w,rx_r,rx_w; static unsigned notified;
static void notify(void *p) { assert(p==&notified); notified++; }
int main(void) {
 nvx_gsp_ring_t tx={0},rx={0}; nvx_gsp_rpc_manager_t m={0};
 nvx_gsp_rpc_handle_t h={0},handles[NVX_GSP_RPC_MAX_PENDING];
 nvx_gsp_rpc_result_t result; nvx_gsp_rpc_view_t unmatched;
 char response[128]={0}; uint32_t seq; const char req[]="request"; const char reply[]="reply-data";
 assert(nvx_gsp_ring_init(&tx,tx_mem,PAGES,&tx_r,&tx_w,notify,&notified)==NVX_GSP_RING_OK);
 assert(nvx_gsp_ring_init(&rx,rx_mem,PAGES,&rx_r,&rx_w,0,0)==NVX_GSP_RING_OK);
 assert(nvx_gsp_rpc_init(&m,&tx,&rx)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_pump(&m,scratch,sizeof(scratch),&unmatched)==NVX_GSP_RPC_PENDING);
 assert(nvx_gsp_ring_send(&rx,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,99U,reply,sizeof(reply),scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 assert(nvx_gsp_rpc_pump(&m,scratch,sizeof(scratch),&unmatched)==NVX_GSP_RPC_UNMATCHED_MESSAGE);
 assert(unmatched.function==99U&&unmatched.payload_size==sizeof(reply));
 rx.next_sequence=1U;
 assert(nvx_gsp_rpc_submit(&m,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,10U,10U,req,sizeof(req),scratch,sizeof(scratch),response,sizeof(response),&h)==NVX_GSP_RPC_OK);
 assert(h.valid&&tx_w==1U&&notified==1U);
 assert(nvx_gsp_rpc_query(&m,&h,&result)==NVX_GSP_RPC_PENDING);
 assert(nvx_gsp_rpc_release(&m,&h)==NVX_GSP_RPC_BUSY);
 nvx_gsp_rpc_handle_t concurrent={0};
 assert(nvx_gsp_rpc_submit(&m,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,11U,11U,req,sizeof(req),scratch,sizeof(scratch),response,sizeof(response),&concurrent)==NVX_GSP_RPC_OK);
 assert(concurrent.valid&&tx_w==2U);
 assert(nvx_gsp_ring_send(&rx,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,11U,reply,sizeof(reply),scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 rx_mem[(size_t)rx_r*NVX_GSP_PAGE_SIZE+48U]=2U;
 rx_mem[(size_t)rx_r*NVX_GSP_PAGE_SIZE+49U]=0U;
 rx_mem[(size_t)rx_r*NVX_GSP_PAGE_SIZE+50U]=0U;
 rx_mem[(size_t)rx_r*NVX_GSP_PAGE_SIZE+51U]=0U;
 assert(nvx_gsp_rpc_pump(&m,scratch,sizeof(scratch),&unmatched)==NVX_GSP_RPC_COMPLETE);
 assert(nvx_gsp_rpc_query(&m,&concurrent,&result)==NVX_GSP_RPC_COMPLETE);
 assert(result.sequence==2U&&result.function==11U);
 assert(nvx_gsp_rpc_release(&m,&concurrent)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_ring_send(&rx,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,10U,reply,sizeof(reply),scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 rx_mem[(size_t)rx_r*NVX_GSP_PAGE_SIZE+48U]=1U;
 rx_mem[(size_t)rx_r*NVX_GSP_PAGE_SIZE+49U]=0U;
 rx_mem[(size_t)rx_r*NVX_GSP_PAGE_SIZE+50U]=0U;
 rx_mem[(size_t)rx_r*NVX_GSP_PAGE_SIZE+51U]=0U;
 assert(nvx_gsp_rpc_pump(&m,scratch,sizeof(scratch),&unmatched)==NVX_GSP_RPC_COMPLETE);
 assert(nvx_gsp_rpc_query(&m,&h,&result)==NVX_GSP_RPC_COMPLETE);
 assert(result.sequence==1U&&result.function==10U&&result.response_size==sizeof(reply));
 assert(memcmp(response,reply,sizeof(reply))==0);
 assert(nvx_gsp_rpc_release(&m,&h)==NVX_GSP_RPC_OK&&!h.valid);
 assert(nvx_gsp_rpc_query(&m,&h,&result)==NVX_GSP_RPC_STALE_HANDLE);

 assert(nvx_gsp_rpc_submit(&m,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,20U,20U,req,sizeof(req),scratch,sizeof(scratch),response,sizeof(response),&h)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_ring_send(&rx,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,21U,reply,sizeof(reply),scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 rx_mem[(size_t)rx_r*NVX_GSP_PAGE_SIZE+48U]=3U;
 assert(nvx_gsp_rpc_pump(&m,scratch,sizeof(scratch),&unmatched)==NVX_GSP_RPC_FUNCTION_MISMATCH);
 assert(nvx_gsp_rpc_release(&m,&h)==NVX_GSP_RPC_OK);

 assert(nvx_gsp_rpc_submit(&m,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,30U,30U,req,sizeof(req),scratch,sizeof(scratch),response,2U,&h)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_ring_send(&rx,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,30U,reply,sizeof(reply),scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 rx_mem[(size_t)rx_r*NVX_GSP_PAGE_SIZE+48U]=4U;
 assert(nvx_gsp_rpc_pump(&m,scratch,sizeof(scratch),&unmatched)==NVX_GSP_RPC_RESPONSE_TOO_LARGE);
 assert(nvx_gsp_rpc_query(&m,&h,&result)==NVX_GSP_RPC_RESPONSE_TOO_LARGE&&result.response_size==sizeof(reply));
 assert(nvx_gsp_rpc_release(&m,&h)==NVX_GSP_RPC_OK);

 memset(large_request,0xA5,sizeof(large_request));
 uint32_t tx_before_large=tx_w;
 assert(nvx_gsp_rpc_submit(&m,NVX_GSP_RPC_HEADER_VERSION_R535,
     NVX_GSP_RPC_SIGNATURE_R535,35U,35U,large_request,sizeof(large_request),
     scratch,sizeof(scratch),response,sizeof(response),&h)==NVX_GSP_RPC_SEND_FAILED);
 assert(!h.valid&&tx_w==tx_before_large);

 assert(nvx_gsp_rpc_submit(&m,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,40U,40U,req,sizeof(req),scratch,sizeof(scratch),response,sizeof(response),&h)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_mark_device_lost(&m)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_query(&m,&h,&result)==NVX_GSP_RPC_DEVICE_LOST);
 assert(nvx_gsp_rpc_release(&m,&h)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_submit(&m,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,1U,1U,req,sizeof(req),scratch,sizeof(scratch),0,0,&h)==NVX_GSP_RPC_DEVICE_LOST);

 /* Reinitialize a drained test manager, then retain 32 completed handles. */
 memset(&m,0,sizeof(m));
 assert(nvx_gsp_rpc_init(&m,&tx,&rx)==NVX_GSP_RPC_OK);
 for(size_t i=0;i<NVX_GSP_RPC_MAX_PENDING;i++) {
   assert(nvx_gsp_rpc_submit(&m,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,100U+(uint32_t)i,100U+(uint32_t)i,
       req,sizeof(req),scratch,sizeof(scratch),0,0,&handles[i])==NVX_GSP_RPC_OK);
   assert(nvx_gsp_ring_send(&rx,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,100U+(uint32_t)i,0,0,
       scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
   rx_mem[(size_t)rx_r*NVX_GSP_PAGE_SIZE+48U]=(uint8_t)seq;
   rx_mem[(size_t)rx_r*NVX_GSP_PAGE_SIZE+49U]=(uint8_t)(seq>>8U);
   rx_mem[(size_t)rx_r*NVX_GSP_PAGE_SIZE+50U]=(uint8_t)(seq>>16U);
   rx_mem[(size_t)rx_r*NVX_GSP_PAGE_SIZE+51U]=(uint8_t)(seq>>24U);
   assert(nvx_gsp_rpc_pump(&m,scratch,sizeof(scratch),&unmatched)==NVX_GSP_RPC_COMPLETE);
 }
 assert(nvx_gsp_rpc_submit(&m,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,999U,999U,req,sizeof(req),scratch,sizeof(scratch),0,0,&h)==NVX_GSP_RPC_NO_SLOTS);
 for(size_t i=0;i<NVX_GSP_RPC_MAX_PENDING;i++) {
   assert(nvx_gsp_rpc_query(&m,&handles[i],&result)==NVX_GSP_RPC_COMPLETE);
   assert(nvx_gsp_rpc_release(&m,&handles[i])==NVX_GSP_RPC_OK);
 }
 assert(nvx_gsp_rpc_mark_device_lost(&m)==NVX_GSP_RPC_OK);
 assert(nvx_gsp_rpc_submit(&m,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,1U,1U,req,sizeof(req),scratch,sizeof(scratch),0,0,&h)==NVX_GSP_RPC_DEVICE_LOST);
 assert(nvx_gsp_rpc_mark_device_lost(0)==NVX_GSP_RPC_INVALID_ARGUMENT);
 return 0;
}
