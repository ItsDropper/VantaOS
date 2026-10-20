/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_msgq.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
static uint8_t store[64U*NVX_GSP_MSGQ_ELEMENT_SIZE] __attribute__((aligned(NVX_GSP_MSGQ_HEADER_PAGE_SIZE)));
static uint8_t small[2U*NVX_GSP_MSGQ_ELEMENT_SIZE] __attribute__((aligned(NVX_GSP_MSGQ_HEADER_PAGE_SIZE)));
static uint8_t large[384U*NVX_GSP_MSGQ_ELEMENT_SIZE] __attribute__((aligned(NVX_GSP_MSGQ_HEADER_PAGE_SIZE)));
static void put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8U); }
static void put32(uint8_t *p,uint32_t v) {
 p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8U);p[2]=(uint8_t)(v>>16U);p[3]=(uint8_t)(v>>24U);
}
int main(void) {
 nvx_gsp_msgq_layout_t tx={0},rx={0};
 assert(nvx_gsp_msgq_prepare_tx(store,sizeof(store),4096U,&tx)==NVX_GSP_MSGQ_OK);
 assert(tx.entries==store+4096U&&tx.entry_offset==4096U&&tx.element_size==4096U);
 assert(tx.backing_store_size==sizeof(store)&&tx.element_count==63U);
 assert(store[0]==2U&&store[1]==0U&&store[2]==0U&&store[3]==0U);
 assert(nvx_gsp_msgq_validate_rx(store,sizeof(store),4096U,&rx)==NVX_GSP_MSGQ_OK);
 assert(rx.entries==tx.entries&&rx.element_count==63U);
 assert(nvx_gsp_msgq_prepare_tx(large,sizeof(large),4096U,&tx)==NVX_GSP_MSGQ_OK);
 assert(tx.element_count==383U&&tx.backing_store_size==sizeof(large));
 assert(nvx_gsp_msgq_validate_rx(large,sizeof(large),4096U,&rx)==NVX_GSP_MSGQ_OK);
 assert(rx.element_count==383U);
 assert(nvx_gsp_msgq_prepare_tx(small,sizeof(small),4096U,&tx)==NVX_GSP_MSGQ_OK);
 assert(tx.element_count==1U);
 assert(nvx_gsp_msgq_validate_rx(small,sizeof(small),4096U,&rx)==NVX_GSP_MSGQ_OK);
 assert(nvx_gsp_msgq_prepare_tx(store,sizeof(store),2048U,&tx)==NVX_GSP_MSGQ_OK);
 assert(tx.element_size==2048U&&tx.element_count==126U);
 assert(nvx_gsp_msgq_validate_rx(store,sizeof(store),2048U,&rx)==NVX_GSP_MSGQ_OK);
 assert(nvx_gsp_msgq_prepare_tx(store,sizeof(store),16U,&tx)==NVX_GSP_MSGQ_OK);
 assert(nvx_gsp_msgq_validate_rx(store,sizeof(store),16U,&rx)==NVX_GSP_MSGQ_OK);
 assert(nvx_gsp_msgq_prepare_tx(store,sizeof(store),65536U,&tx)==NVX_GSP_MSGQ_OK);
 assert(tx.element_count==3U);
 assert(nvx_gsp_msgq_validate_rx(store,sizeof(store),65536U,&rx)==NVX_GSP_MSGQ_OK);
 assert(nvx_gsp_msgq_prepare_tx(store,sizeof(store),8U,&tx)==NVX_GSP_MSGQ_BAD_SIZE);
 assert(nvx_gsp_msgq_prepare_tx(store,sizeof(store),4096U,0)==NVX_GSP_MSGQ_INVALID_ARGUMENT);
 assert(nvx_gsp_msgq_prepare_tx(0,sizeof(store),4096U,&tx)==NVX_GSP_MSGQ_INVALID_ARGUMENT);
 assert(nvx_gsp_msgq_prepare_tx(store+1U,sizeof(store)-1U,4096U,&tx)==NVX_GSP_MSGQ_BAD_ALIGNMENT);
 assert(nvx_gsp_msgq_validate_rx(0,sizeof(store),4096U,&rx)==NVX_GSP_MSGQ_INVALID_ARGUMENT);
 put16(store,3U);
 assert(nvx_gsp_msgq_validate_rx(store,sizeof(store),4096U,&rx)==NVX_GSP_MSGQ_BAD_VERSION);
 assert(nvx_gsp_msgq_prepare_tx(store,sizeof(store),4096U,&tx)==NVX_GSP_MSGQ_OK);
 put32(store+4U,(uint32_t)sizeof(store)-4096U);
 assert(nvx_gsp_msgq_validate_rx(store,sizeof(store),4096U,&rx)==NVX_GSP_MSGQ_BAD_LAYOUT);
 assert(nvx_gsp_msgq_prepare_tx(store,sizeof(store),4096U,&tx)==NVX_GSP_MSGQ_OK);
 put32(store+8U,2048U);
 assert(nvx_gsp_msgq_validate_rx(store,sizeof(store),4096U,&rx)==NVX_GSP_MSGQ_BAD_LAYOUT);
 assert(nvx_gsp_msgq_prepare_tx(store,sizeof(store),4096U,&tx)==NVX_GSP_MSGQ_OK);
 put32(store+12U,62U);
 assert(nvx_gsp_msgq_validate_rx(store,sizeof(store),4096U,&rx)==NVX_GSP_MSGQ_BAD_LAYOUT);
 assert(nvx_gsp_msgq_prepare_tx(store,sizeof(store),4096U,&tx)==NVX_GSP_MSGQ_OK);
 put32(store+16U,32U);
 assert(nvx_gsp_msgq_validate_rx(store,sizeof(store),4096U,&rx)==NVX_GSP_MSGQ_BAD_LAYOUT);
 assert(nvx_gsp_msgq_prepare_tx(store,sizeof(store),4096U,&tx)==NVX_GSP_MSGQ_OK);
 put32(store+20U,1U);
 assert(nvx_gsp_msgq_validate_rx(store,sizeof(store),4096U,&rx)==NVX_GSP_MSGQ_BAD_RESERVED);
 return 0;
}
