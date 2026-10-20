/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_msgq_queue.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define BYTES (8U*NVX_GSP_MSGQ_ELEMENT_SIZE)
static uint8_t tx_store[BYTES] __attribute__((aligned(NVX_GSP_MSGQ_HEADER_PAGE_SIZE)));
static uint8_t rx_store[BYTES] __attribute__((aligned(NVX_GSP_MSGQ_HEADER_PAGE_SIZE)));
typedef struct fake_io { uint32_t regs[16];int fail_read,fail_write;unsigned before,after; } fake_io_t;
static int read32(void *p,uint32_t reg,uint32_t *out) {
 fake_io_t *f=p;if(f->fail_read||reg/4U>=16U)return 0;*out=f->regs[reg/4U];return 1;
}
static int write32(void *p,uint32_t reg,uint32_t v) {
 fake_io_t *f=p;if(f->fail_write||reg/4U>=16U)return 0;f->regs[reg/4U]=v;return 1;
}
static void before(void *p) { ((fake_io_t *)p)->before++; }
static void after(void *p) { ((fake_io_t *)p)->after++; }
int main(void) {
 fake_io_t f={0};nvx_gsp_msgq_io_t io={read32,write32,before,after,&f};
 nvx_gsp_msgq_queue_t tx={0},rx={0};uint8_t *w;const uint8_t *r;
 assert(nvx_gsp_msgq_queue_init_tx(&tx,tx_store,sizeof(tx_store),4096U,&io,4U,8U)==NVX_GSP_MSGQ_QUEUE_OK);
 assert(tx.layout.element_count==7U&&f.regs[1]==0U);
 nvx_gsp_msgq_layout_t simulated_gsp_header={0};
 assert(nvx_gsp_msgq_prepare_tx(rx_store,sizeof(rx_store),4096U,&simulated_gsp_header)==NVX_GSP_MSGQ_OK);
 assert(nvx_gsp_msgq_queue_init_rx(&rx,rx_store,sizeof(rx_store),4096U,&io,12U,16U)==NVX_GSP_MSGQ_QUEUE_OK);
 assert(rx.layout.element_count==7U&&f.regs[4]==0U);
 assert(nvx_gsp_msgq_tx_get(&tx,0U,&w)==NVX_GSP_MSGQ_QUEUE_OK&&w==tx_store+4096U);
 memset(w,0xA5,4096U);
 assert(nvx_gsp_msgq_tx_get(&tx,6U,&w)==NVX_GSP_MSGQ_QUEUE_OK);
 assert(nvx_gsp_msgq_tx_get(&tx,7U,&w)==NVX_GSP_MSGQ_QUEUE_FULL&&w==0);
 assert(nvx_gsp_msgq_tx_submit(&tx,7U)==NVX_GSP_MSGQ_QUEUE_OK&&f.regs[1]==7U);
 assert(nvx_gsp_msgq_tx_get(&tx,0U,&w)==NVX_GSP_MSGQ_QUEUE_FULL);
 f.regs[2]=3U;
 assert(nvx_gsp_msgq_tx_get(&tx,0U,&w)==NVX_GSP_MSGQ_QUEUE_OK&&w==tx_store+4096U);
 assert(nvx_gsp_msgq_tx_submit(&tx,5U)==NVX_GSP_MSGQ_QUEUE_FULL);
 assert(nvx_gsp_msgq_tx_submit(&tx,3U)==NVX_GSP_MSGQ_QUEUE_OK&&f.regs[1]==10U);
 f.regs[3]=2U;
 assert(nvx_gsp_msgq_rx_get(&rx,0U,&r)==NVX_GSP_MSGQ_QUEUE_OK&&r==rx_store+4096U);
 assert(nvx_gsp_msgq_rx_get(&rx,2U,&r)==NVX_GSP_MSGQ_QUEUE_EMPTY&&r==0);
 assert(nvx_gsp_msgq_rx_consume(&rx,2U)==NVX_GSP_MSGQ_QUEUE_OK&&f.regs[4]==2U);
 assert(f.before>=3U&&f.after>=1U);
 f.fail_read=1;
 assert(nvx_gsp_msgq_tx_get(&tx,0U,&w)==NVX_GSP_MSGQ_QUEUE_IO_ERROR);
 f.fail_read=0;f.regs[2]=10U;f.fail_write=1;
 assert(nvx_gsp_msgq_tx_submit(&tx,1U)==NVX_GSP_MSGQ_QUEUE_IO_ERROR);
 f.fail_write=0;
 f.regs[2]=UINT32_MAX;
 assert(nvx_gsp_msgq_tx_get(&tx,0U,&w)==NVX_GSP_MSGQ_QUEUE_BAD_COUNTER);
 nvx_gsp_msgq_queue_t invalid={0};
 assert(nvx_gsp_msgq_queue_init_tx(&invalid,tx_store,sizeof(tx_store),4096U,&io,4U,4U)==NVX_GSP_MSGQ_QUEUE_INVALID_ARGUMENT);
 return 0;
}
