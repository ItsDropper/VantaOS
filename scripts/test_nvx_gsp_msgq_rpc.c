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
static uint8_t large_payload[5000U];
typedef struct fake {uint32_t regs[8];} fake_t;
static int rd(void *p,uint32_t r,uint32_t *o){fake_t*f=p;if(r/4U>=8U)return 0;*o=f->regs[r/4U];return 1;}
static int wr(void *p,uint32_t r,uint32_t v){fake_t*f=p;if(r/4U>=8U)return 0;f->regs[r/4U]=v;return 1;}
static void barrier(void *p){(void)p;__atomic_thread_fence(__ATOMIC_SEQ_CST);}
static uint32_t get32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U);}
static void put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8U);p[2]=(uint8_t)(v>>16U);p[3]=(uint8_t)(v>>24U);}
static void make_reply(uint8_t *dst,size_t capacity,uint32_t function,
                        uint32_t sequence,const void *payload,size_t payload_size,
                        uint32_t nvdm)
{
 size_t message_size=NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE+payload_size;
 size_t slots=(message_size+NVX_GSP_PAGE_SIZE-1U)/NVX_GSP_PAGE_SIZE;
 assert(capacity>=slots*NVX_GSP_PAGE_SIZE);
 memset(dst,0,slots*NVX_GSP_PAGE_SIZE);
 put32(dst,NVX_GSP_MCTP_MAGIC);put32(dst+4U,(uint32_t)message_size);
 put32(dst+8U,NVX_GSP_MCTP_HEADER_SINGLE);put32(dst+12U,nvdm);
 put32(dst+16U,(uint32_t)(NVX_GSP_RPC_HEADER_SIZE+payload_size));put32(dst+20U,0U);
 put32(dst+24U,NVX_GSP_RPC_HEADER_VERSION_R535);put32(dst+28U,NVX_GSP_RPC_SIGNATURE_R535);
 put32(dst+32U,(uint32_t)(NVX_GSP_RPC_HEADER_SIZE+payload_size));put32(dst+36U,function);
 put32(dst+40U,0U);put32(dst+44U,0U);put32(dst+48U,sequence);
 if(payload_size)memcpy(dst+NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE,payload,payload_size);
}
int main(void){
 fake_t f={0};nvx_gsp_msgq_io_t io={rd,wr,barrier,barrier,&f};
 nvx_gsp_msgq_queue_t tx={0},rx={0};nvx_gsp_msgq_rpc_transport_t transport={0};
 nvx_gsp_msgq_layout_t fake_gsp={0};uint32_t seq=0U;nvx_gsp_rpc_view_t view;
 const char payload[]="NVIDIA-GSP-RPC-payload";
 memset(large_payload,0x6BU,sizeof(large_payload));
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
 uint8_t *reply=rx_store+2U*NVX_GSP_PAGE_SIZE;
 make_reply(reply,NVX_GSP_PAGE_SIZE,0x1235U,77U,payload,sizeof(payload),NVX_GSP_NVDM_HEADER_RM_RPC);
 f.regs[2]=1U;
 assert(nvx_gsp_msgq_rpc_receive(&transport,scratch,sizeof(scratch),&view)==NVX_GSP_MSGQ_RPC_OK);
 assert(view.function==0x1235U&&view.sequence==77U&&view.payload_size==sizeof(payload));
 assert(memcmp(view.payload,payload,sizeof(payload))==0&&f.regs[3]==1U);
 assert(nvx_gsp_msgq_rpc_receive(&transport,scratch,sizeof(scratch),&view)==NVX_GSP_MSGQ_RPC_QUEUE_EMPTY);
 /* A large response spans two slots and wraps the consumer counter forward. */
 make_reply(rx_store+2U*NVX_GSP_PAGE_SIZE,NVX_GSP_PAGE_SIZE*2U,
     0x1236U,78U,large_payload,sizeof(large_payload),NVX_GSP_NVDM_HEADER_RM_RPC);
 f.regs[2]=3U;
 assert(nvx_gsp_msgq_rpc_receive(&transport,scratch,sizeof(scratch),&view)==NVX_GSP_MSGQ_RPC_OK);
 assert(view.function==0x1236U&&view.sequence==78U&&view.element_pages==2U);
 assert(view.payload_size==sizeof(large_payload)&&memcmp(view.payload,large_payload,sizeof(large_payload))==0);
 assert(f.regs[3]==3U);
 /* Malformed NVDM framing must not advance the peer-owned message. */
 make_reply(rx_store+4U*NVX_GSP_PAGE_SIZE,NVX_GSP_PAGE_SIZE,0x1237U,79U,
     payload,sizeof(payload),0xDEADBEEFU);
 f.regs[2]=4U;
 assert(nvx_gsp_msgq_rpc_receive(&transport,scratch,sizeof(scratch),&view)==NVX_GSP_MSGQ_RPC_BAD_MESSAGE);
 assert(f.regs[3]==3U);
 make_reply(rx_store+4U*NVX_GSP_PAGE_SIZE,NVX_GSP_PAGE_SIZE,0x1237U,79U,
     payload,sizeof(payload),NVX_GSP_NVDM_HEADER_RM_RPC);
 assert(nvx_gsp_msgq_rpc_receive(&transport,scratch,sizeof(scratch),&view)==NVX_GSP_MSGQ_RPC_OK);
 assert(view.function==0x1237U&&f.regs[3]==4U);
 assert(nvx_gsp_msgq_rpc_send(&transport,0x999U,large_payload,sizeof(large_payload),scratch,sizeof(scratch),&seq)==NVX_GSP_MSGQ_RPC_OK);
 assert(seq==3U&&f.regs[0]==4U);
 /* Fill through the physical end, then prove that the next record wraps to slot zero. */
 f.regs[1]=4U;
 for(unsigned i=0;i<15U;i++)
     assert(nvx_gsp_msgq_rpc_send(&transport,0x2000U+i,payload,sizeof(payload),scratch,sizeof(scratch),&seq)==NVX_GSP_MSGQ_RPC_OK);
 assert(f.regs[0]==19U);
 assert(nvx_gsp_msgq_rpc_send(&transport,0x3000U,payload,sizeof(payload),scratch,sizeof(scratch),&seq)==NVX_GSP_MSGQ_RPC_QUEUE_FULL);
 f.regs[1]=19U;
 assert(nvx_gsp_msgq_rpc_send(&transport,0x3001U,payload,sizeof(payload),scratch,sizeof(scratch),&seq)==NVX_GSP_MSGQ_RPC_OK);
 assert(f.regs[0]==20U&&get32(tx_store+NVX_GSP_PAGE_SIZE+36U)==0x3001U);
 assert(nvx_gsp_msgq_rpc_send(&transport,0x999U,payload,
     NVX_GSP_MSGQ_RPC_MAX_ELEMENTS*NVX_GSP_PAGE_SIZE,scratch,sizeof(scratch),&seq)==NVX_GSP_MSGQ_RPC_BUFFER_TOO_SMALL);
 assert(nvx_gsp_msgq_rpc_init(&transport,&tx,&rx)==NVX_GSP_MSGQ_RPC_INVALID_ARGUMENT);
 return 0;
}
