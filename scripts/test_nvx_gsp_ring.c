/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_ring.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
static uint8_t ring_mem[4U*NVX_GSP_PAGE_SIZE] __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
static uint8_t scratch[4U*NVX_GSP_PAGE_SIZE] __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
static uint32_t read_index,write_index; static unsigned notifications;
static void notify(void *p) { assert(p==&notifications); notifications++; }
int main(void) {
 nvx_gsp_ring_t ring={0}; nvx_gsp_rpc_view_t view;
 uint8_t payload[5000]; uint32_t seq=0; memset(payload,0x5A,sizeof(payload));
 assert(nvx_gsp_ring_init(0,ring_mem,4,&read_index,&write_index,notify,&notifications)==NVX_GSP_RING_INVALID_ARGUMENT);
 assert(nvx_gsp_ring_init(&ring,ring_mem,1,&read_index,&write_index,notify,&notifications)==NVX_GSP_RING_INVALID_ARGUMENT);
 assert(nvx_gsp_ring_init(&ring,(void *)(ring_mem+1),4,&read_index,&write_index,notify,&notifications)==NVX_GSP_RING_INVALID_ARGUMENT);
 read_index=4; assert(nvx_gsp_ring_init(&ring,ring_mem,4,&read_index,&write_index,notify,&notifications)==NVX_GSP_RING_BAD_POINTER); read_index=0;
 assert(nvx_gsp_ring_init(&ring,ring_mem,4,&read_index,&write_index,notify,&notifications)==NVX_GSP_RING_OK);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_QUEUE_EMPTY);
 assert(nvx_gsp_ring_send(&ring,1U,0xAABBCCDDU,0x1234U,payload,sizeof(payload),scratch,4095U,&seq)==NVX_GSP_RING_BUFFER_TOO_SMALL);
 assert(nvx_gsp_ring_send(&ring,1U,0xAABBCCDDU,0x1234U,0,1U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_INVALID_ARGUMENT);
 assert(nvx_gsp_ring_send(&ring,1U,0xAABBCCDDU,0x1234U,payload,sizeof(payload),scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 assert(seq==1U&&notifications==1U&&write_index==2U);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK);
 assert(view.header_version==1U&&view.signature==0xAABBCCDDU&&view.function==0x1234U);
 assert(view.length==NVX_GSP_RPC_HEADER_SIZE+sizeof(payload));
 assert(view.payload_size==sizeof(payload)&&memcmp(view.payload,payload,sizeof(payload))==0);
 assert(view.sequence==seq&&view.message_sequence==seq&&view.element_pages==2U);
 assert(read_index==write_index&&notifications==1U);
 /* Fill the ring with three one-page messages; the fourth must be rejected. */
 for(unsigned i=0;i<3U;i++) assert(nvx_gsp_ring_send(&ring,2U,3U,100U+i,payload,32U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 assert(nvx_gsp_ring_send(&ring,2U,3U,104U,payload,32U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_QUEUE_FULL);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK&&view.function==100U);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK&&view.function==101U);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK&&view.function==102U);
 assert(nvx_gsp_ring_send(&ring,2U,3U,201U,payload,32U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK&&view.function==201U);
 assert(nvx_gsp_ring_send(&ring,2U,3U,202U,payload,32U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK&&view.function==202U);
 assert(read_index==3U&&write_index==3U);
 assert(nvx_gsp_ring_send(&ring,2U,3U,200U,payload,5000U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 assert(write_index==1U);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK&&view.function==200U);
 assert(read_index==write_index);
 /* Corrupt data is rejected without consuming the entry. */
 assert(nvx_gsp_ring_send(&ring,1U,2U,300U,payload,8U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_OK);
 ring_mem[(size_t)read_index*NVX_GSP_PAGE_SIZE+NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE]^=1U;
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_BAD_CHECKSUM);
 ring_mem[(size_t)read_index*NVX_GSP_PAGE_SIZE+NVX_GSP_MSG_HEADER_SIZE+NVX_GSP_RPC_HEADER_SIZE]^=1U;
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_OK&&view.function==300U);
 assert(nvx_gsp_ring_send(&ring,1U,2U,301U,payload,65457U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_MESSAGE_TOO_LARGE);
 assert(nvx_gsp_ring_send(&ring,1U,2U,301U,scratch,16U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_INVALID_ARGUMENT);
 ring.initialized=0U;
 assert(nvx_gsp_ring_send(&ring,1U,2U,1U,payload,1U,scratch,sizeof(scratch),&seq)==NVX_GSP_RING_NOT_INITIALIZED);
 assert(nvx_gsp_ring_receive(&ring,scratch,sizeof(scratch),&view)==NVX_GSP_RING_NOT_INITIALIZED);
 return 0;
}
