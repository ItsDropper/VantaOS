/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_ring.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define PAGES NVX_GSP_MAX_QUEUE_PAGES
static uint8_t ring_mem[4U*NVX_GSP_PAGE_SIZE] __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
static uint8_t wrap_mem[2U*NVX_GSP_PAGE_SIZE] __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
static uint8_t scratch[16U*NVX_GSP_PAGE_SIZE] __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
static uint32_t read_index,write_index; static uint32_t wrap_read,wrap_write;
static unsigned notifications;
static uint32_t read32(const uint8_t *p) {
 return (uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U);
}
static void notify(void *p) { assert(p==&notifications); notifications++; }
int main(void) {
 nvx_gsp_ring_t ring={0}; nvx_gsp_rpc_view_t view;
 uint8_t payload[5000]; uint32_t seq=0; memset(payload,0x5A,sizeof(payload));
 assert(nvx_gsp_ring_init(0,ring_mem,4,&read_index,&write_index,notify,&notifications)==NVX_GSP_RING_INVALID_ARGUMENT);
 assert(nvx_gsp_ring_init(&ring,ring_mem,1,&read_index,&write_index,notify,&notifications)==NVX_GSP_RING_INVALID_ARGUMENT);
 assert(nvx_gsp_ring_init(&ring,ring_mem,4,(volatile uint32_t *)ring_mem,&write_index,notify,&notifications)==NVX_GSP_RING_INVALID_ARGUMENT);
 assert(nvx_gsp_ring_init(&ring,ring_mem,4,&read_index,&read_index,notify,&notifications)==NVX_GSP_RING_INVALID_ARGUMENT);
 assert(nvx_gsp_ring_init(&ring,(void *)(ring_mem+1),4,&read_index,&write_index,notify,&notifications)==NVX_GSP_RING_INVALID_ARGUMENT);
 read_index=4; assert(nvx_gsp_ring_init(&ring,ring_mem,4,&read_index,&write_index,notify,&notifications)==NVX_GSP_RING_BAD_POINTER); read_index=0;
 assert(nvx_gsp_ring_init(&ring,ring_mem,4,&read_index,&write_index,notify,&notifications)==NVX_GSP_RING_OK);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_QUEUE_EMPTY);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),(nvx_gsp_rpc_view_t *)scratch)==NVX_GSP_RING_INVALID_ARGUMENT);
 assert(nvx_gsp_ring_send(&ring,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,0x1234U,payload,sizeof(payload),scratch,4095U,&seq)==NVX_GSP_RING_BUFFER_TOO_SMALL);
 assert(nvx_gsp_ring_send(&ring,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,0x1234U,0,1U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_INVALID_ARGUMENT);
 assert(nvx_gsp_ring_send(&ring,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,0x1234U,payload,sizeof(payload),scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 assert(seq==1U&&notifications==1U&&write_index==2U);
 assert(read32(ring_mem)==NVX_GSP_MCTP_MAGIC);
 assert(read32(ring_mem+4U)==NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE+sizeof(payload));
 assert(read32(ring_mem+8U)==NVX_GSP_MCTP_HEADER_SINGLE);
 assert(read32(ring_mem+12U)==NVX_GSP_NVDM_HEADER_RM_RPC);
 assert(read32(ring_mem+16U)==NVX_GSP_RPC_HEADER_SIZE+sizeof(payload));
 assert(read32(ring_mem+20U)==0U);
 assert(nvx_gsp_ring_send(&ring,1U,2U,0x1234U,payload,1U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_BAD_MESSAGE);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK);
 assert(view.header_version==NVX_GSP_RPC_HEADER_VERSION_R535&&
         view.signature==NVX_GSP_RPC_SIGNATURE_R535&&view.function==0x1234U);
 assert(view.length==NVX_GSP_RPC_HEADER_SIZE+sizeof(payload));
 assert(view.payload_size==sizeof(payload)&&memcmp(view.payload,payload,sizeof(payload))==0);
 assert(view.sequence==1U&&view.message_sequence==0U&&view.element_pages==2U);
 assert(read_index==write_index&&notifications==1U);
 /* NVIDIA MSGQ uses monotonic counters; all four queue slots are usable. */
 for(unsigned i=0;i<4U;i++)
   assert(nvx_gsp_ring_send(&ring,NVX_GSP_RPC_HEADER_VERSION_R535,
     NVX_GSP_RPC_SIGNATURE_R535,100U+i,payload,32U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 assert(nvx_gsp_ring_send(&ring,NVX_GSP_RPC_HEADER_VERSION_R535,
   NVX_GSP_RPC_SIGNATURE_R535,104U,payload,32U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_QUEUE_FULL);
 for(unsigned i=0;i<4U;i++)
   assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK&&view.function==100U+i);
 assert(read_index==write_index&&write_index==6U);
 assert(nvx_gsp_ring_send(&ring,NVX_GSP_RPC_HEADER_VERSION_R535,
   NVX_GSP_RPC_SIGNATURE_R535,201U,payload,32U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK&&view.function==201U);
 assert(nvx_gsp_ring_send(&ring,NVX_GSP_RPC_HEADER_VERSION_R535,
   NVX_GSP_RPC_SIGNATURE_R535,202U,payload,32U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK&&view.function==202U);
 assert(read_index==write_index&&write_index==8U);
 assert(nvx_gsp_ring_send(&ring,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,200U,payload,5000U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 assert(write_index==10U);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK&&view.function==200U);
 assert(read_index==write_index);
 /* Corrupt data is rejected without consuming the entry. */
 assert(nvx_gsp_ring_send(&ring,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,300U,payload,8U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 ring_mem[(size_t)(read_index%4U)*NVX_GSP_PAGE_SIZE+28U]^=1U;
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_BAD_MESSAGE);
 ring_mem[(size_t)(read_index%4U)*NVX_GSP_PAGE_SIZE+28U]^=1U;
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK&&view.function==300U);
 assert(nvx_gsp_ring_send(&ring,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,301U,payload,NVX_GSP_MAX_RPC_PAYLOAD+1U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_MESSAGE_TOO_LARGE);
 assert(nvx_gsp_ring_send(&ring,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,301U,scratch,16U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_INVALID_ARGUMENT);
 ring.initialized=0U;
 assert(nvx_gsp_ring_send(&ring,NVX_GSP_RPC_HEADER_VERSION_R535,NVX_GSP_RPC_SIGNATURE_R535,1U,payload,1U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_NOT_INITIALIZED);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_NOT_INITIALIZED);

 /* Monotonic MSGQ counters wrap naturally at 32 bits. */
 nvx_gsp_ring_t wrap_ring={0};
 wrap_read=UINT32_MAX-1U; wrap_write=UINT32_MAX-1U;
 assert(nvx_gsp_ring_init(&wrap_ring,wrap_mem,2U,&wrap_read,&wrap_write,0,0)==NVX_GSP_RING_OK);
 assert(nvx_gsp_ring_send(&wrap_ring,NVX_GSP_RPC_HEADER_VERSION_R535,
     NVX_GSP_RPC_SIGNATURE_R535,400U,payload,5000U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 assert(wrap_write==0U&&wrap_read==UINT32_MAX-1U);
 assert(nvx_gsp_ring_receive(&wrap_ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK);
 assert(view.function==400U&&view.element_pages==2U);
 assert(wrap_read==0U&&wrap_write==0U);
 return 0;
}
