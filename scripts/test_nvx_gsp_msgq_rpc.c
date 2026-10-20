/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_msgq_rpc.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define PAGES 20U
#define BYTES (PAGES*NVX_GSP_PAGE_SIZE)
static uint8_t tx_store[BYTES] __attribute__((aligned(NVX_GSP_MSGQ_HEADER_PAGE_SIZE)));
static uint8_t rx_store[BYTES] __attribute__((aligned(NVX_GSP_MSGQ_HEADER_PAGE_SIZE)));
static uint8_t scratch[16U*NVX_GSP_PAGE_SIZE] __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
typedef struct fake {uint32_t regs[8];} fake_t;
static int rd(void *p,uint32_t r,uint32_t *o){fake_t*f=p;if(r/4U>=8U)return 0;*o=f->regs[r/4U];return 1;}
static int wr(void *p,uint32_t r,uint32_t v){fake_t*f=p;if(r/4U>=8U)return 0;f->regs[r/4U]=v;return 1;}
static void barrier(void *p){(void)p;__atomic_thread_fence(__ATOMIC_SEQ_CST);}
static uint32_t get32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U);}
static void put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8U);p[2]=(uint8_t)(v>>16U);p[3]=(uint8_t)(v>>24U);}
int main(void){
 fake_t f={0};nvx_gsp_msgq_io_t io={rd,wr,barrier,barrier,&f};
 nvx_gsp_msgq_queue_t tx={0},rx={0};nvx_gsp_msgq_rpc_transport_t transport={0};
 nvx_gsp_msgq_layout_t fake_gsp={0};uint32_t seq=0U;nvx_gsp_rpc_view_t view;
 const char payload[]="NVIDIA-GSP-RPC-payload";
 assert(nvx_gsp_msgq_queue_init_tx(&tx,tx_store,sizeof(tx_store),4096U,&io,0U,4U)==NVX_GSP_MSGQ_QUEUE_OK);
 assert(nvx_gsp_msgq_prepare_tx(rx_store,sizeof(rx_store),4096U,&fake_gsp)==NVX_GSP_MSGQ_OK);
 assert(nvx_gsp_msgq_queue_init_rx(&rx,rx_store,sizeof(rx_store),4096U,&io,8U,12U)==NVX_GSP_MSGQ_QUEUE_OK);
 assert(nvx_gsp_msgq_rpc_init(&transport,&tx,&rx)==NVX_GSP_MSGQ_RPC_OK);
 assert(nvx_gsp_msgq_rpc_send(&transport,0x1234U,payload,sizeof(payload),scratch,sizeof(scratch),&seq)==NVX_GSP_MSGQ_RPC_OK);
 assert(seq==1U&&f.regs[0]==1U);
 assert(get32(tx_store+4096U)==NVX_GSP_MCTP_MAGIC);
 assert(get32(tx_store+4096U+4U)==NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE+sizeof(payload));
 assert(get32(tx_store+4096U+48U)==1U);
 assert(nvx_gsp_msgq_rpc_send(&transport,0x1235U,payload,sizeof(payload),scratch,sizeof(scratch),&seq)==NVX_GSP_MSGQ_RPC_OK);
 assert(seq==2U&&f.regs[0]==2U);
 /* Simulate a one-element GSP reply in the RX backing store. */
 uint8_t *reply=rx_store+4096U;memset(reply,0,NVX_GSP_PAGE_SIZE);
 put32(reply,NVX_GSP_MCTP_MAGIC);
 put32(reply+4U,NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE+sizeof(payload));
 put32(reply+8U,NVX_GSP_MCTP_HEADER_SINGLE);put32(reply+12U,NVX_GSP_NVDM_HEADER_RM_RPC);
 put32(reply+16U,NVX_GSP_RPC_HEADER_SIZE+sizeof(payload));put32(reply+20U,0U);
 put32(reply+24U,NVX_GSP_RPC_HEADER_VERSION_R535);put32(reply+28U,NVX_GSP_RPC_SIGNATURE_R535);
 put32(reply+32U,NVX_GSP_RPC_HEADER_SIZE+sizeof(payload));put32(reply+36U,0x1235U);
 put32(reply+40U,0U);put32(reply+44U,0U);put32(reply+48U,77U);
 memcpy(reply+NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE,payload,sizeof(payload));
 f.regs[2]=1U;
 assert(nvx_gsp_msgq_rpc_receive(&transport,scratch,sizeof(scratch),&view)==NVX_GSP_MSGQ_RPC_OK);
 assert(view.function==0x1235U&&view.sequence==77U&&view.payload_size==sizeof(payload));
 assert(memcmp(view.payload,payload,sizeof(payload))==0&&f.regs[3]==1U);
 assert(nvx_gsp_msgq_rpc_receive(&transport,scratch,sizeof(scratch),&view)==NVX_GSP_MSGQ_RPC_QUEUE_EMPTY);
 assert(nvx_gsp_msgq_rpc_send(&transport,0x999U,payload,5000U,scratch,sizeof(scratch),&seq)==NVX_GSP_MSGQ_RPC_OK);
 assert(seq==3U&&f.regs[0]==4U);
 assert(nvx_gsp_msgq_rpc_send(&transport,0x999U,payload,
     NVX_GSP_MSGQ_RPC_MAX_ELEMENTS*NVX_GSP_PAGE_SIZE,scratch,sizeof(scratch),&seq)==NVX_GSP_MSGQ_RPC_BUFFER_TOO_SMALL);
 assert(nvx_gsp_msgq_rpc_init(&transport,&tx,&rx)==NVX_GSP_MSGQ_RPC_INVALID_ARGUMENT);
 return 0;
}
