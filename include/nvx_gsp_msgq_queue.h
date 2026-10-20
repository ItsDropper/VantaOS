/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_MSGQ_QUEUE_H
#define VANTA_NVX_GSP_MSGQ_QUEUE_H
#include <stddef.h>
#include <stdint.h>
#include "nvx_gsp_msgq.h"
typedef int (*nvx_gsp_msgq_read32_fn)(void *context,uint32_t reg,uint32_t *out);
typedef int (*nvx_gsp_msgq_write32_fn)(void *context,uint32_t reg,uint32_t value);
typedef void (*nvx_gsp_msgq_barrier_fn)(void *context);
typedef struct nvx_gsp_msgq_io {
    nvx_gsp_msgq_read32_fn read32;
    nvx_gsp_msgq_write32_fn write32;
    nvx_gsp_msgq_barrier_fn before_device;
    nvx_gsp_msgq_barrier_fn after_device;
    void *context;
} nvx_gsp_msgq_io_t;
typedef enum nvx_gsp_msgq_queue_status {
    NVX_GSP_MSGQ_QUEUE_OK=0,
    NVX_GSP_MSGQ_QUEUE_INVALID_ARGUMENT,
    NVX_GSP_MSGQ_QUEUE_NOT_INITIALIZED,
    NVX_GSP_MSGQ_QUEUE_FULL,
    NVX_GSP_MSGQ_QUEUE_EMPTY,
    NVX_GSP_MSGQ_QUEUE_BAD_COUNTER,
    NVX_GSP_MSGQ_QUEUE_IO_ERROR,
    NVX_GSP_MSGQ_QUEUE_BAD_LAYOUT
} nvx_gsp_msgq_queue_status_t;
typedef struct nvx_gsp_msgq_queue {
    nvx_gsp_msgq_layout_t layout;
    nvx_gsp_msgq_io_t io;
    uint8_t *backing_store;
    uint32_t head_register,tail_register;
    uint32_t local_counter,peer_counter;
    uint8_t transmit,initialized;
} nvx_gsp_msgq_queue_t;
/*
 * Zero-initialize the queue before init. One producer owns TX and one consumer
 * owns RX, matching NVIDIA MSGQ. head/tail offsets come from the verified GPU
 * register map; do not guess register offsets or share a queue between producers.
 * Call tx_get for each slot, fill exactly element_size bytes, then tx_submit.
 * RX mirrors this with rx_get/rx_consume. The callback layer owns MMIO ordering.
 */
nvx_gsp_msgq_queue_status_t nvx_gsp_msgq_queue_init_tx(
    nvx_gsp_msgq_queue_t *,void *backing_store,size_t backing_size,size_t element_size,
    const nvx_gsp_msgq_io_t *,uint32_t head_register,uint32_t tail_register);
nvx_gsp_msgq_queue_status_t nvx_gsp_msgq_queue_init_rx(
    nvx_gsp_msgq_queue_t *,void *backing_store,size_t backing_size,size_t element_size,
    const nvx_gsp_msgq_io_t *,uint32_t head_register,uint32_t tail_register);
nvx_gsp_msgq_queue_status_t nvx_gsp_msgq_tx_get(
    nvx_gsp_msgq_queue_t *,uint32_t index,uint8_t **out_element);
nvx_gsp_msgq_queue_status_t nvx_gsp_msgq_tx_submit(
    nvx_gsp_msgq_queue_t *,uint32_t element_count);
nvx_gsp_msgq_queue_status_t nvx_gsp_msgq_rx_get(
    nvx_gsp_msgq_queue_t *,uint32_t index,const uint8_t **out_element);
nvx_gsp_msgq_queue_status_t nvx_gsp_msgq_rx_consume(
    nvx_gsp_msgq_queue_t *,uint32_t element_count);
#endif
