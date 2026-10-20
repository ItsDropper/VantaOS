/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_GA10X_H
#define VANTA_NVX_GSP_GA10X_H
#include <stddef.h>
#include <stdint.h>
#include "nvx_gsp_msgq_rpc.h"
/* Offsets follow NVIDIA's published TU102/GA102 GSP register definitions. */
#define NVX_GSP_GA10X_MAILBOX(i) (0x110804U+((uint32_t)(i)*4U))
#define NVX_GSP_GA10X_MAILBOX_COUNT 4U
#define NVX_GSP_GA10X_QUEUE_HEAD(i) (0x110C00U+((uint32_t)(i)*8U))
#define NVX_GSP_GA10X_QUEUE_TAIL(i) (0x110C04U+((uint32_t)(i)*8U))
#define NVX_GSP_GA10X_MSGQ_HEAD(i) (0x110C80U+((uint32_t)(i)*8U))
#define NVX_GSP_GA10X_MSGQ_TAIL(i) (0x110C84U+((uint32_t)(i)*8U))
#define NVX_GSP_GA10X_QUEUE_COUNT 8U
#define NVX_GSP_GA10X_RM_QUEUE_INDEX 0U
#define NVX_GSP_GA10X_MSGQ_REQUIRED_BAR_BYTES (NVX_GSP_GA10X_MSGQ_TAIL(0U)+4U)
typedef enum nvx_gsp_ga10x_status {
    NVX_GSP_GA10X_OK=0,
    NVX_GSP_GA10X_INVALID_ARGUMENT,
    NVX_GSP_GA10X_BAD_BAR,
    NVX_GSP_GA10X_ALREADY_INITIALIZED,
    NVX_GSP_GA10X_QUEUE_NOT_READY,
    NVX_GSP_GA10X_QUEUE_ERROR
} nvx_gsp_ga10x_status_t;
typedef struct nvx_gsp_ga10x {
    nvx_gsp_msgq_mmio_context_t mmio_context;
    nvx_gsp_msgq_io_t io;
    nvx_gsp_msgq_queue_t command_queue;
    nvx_gsp_msgq_queue_t response_queue;
    nvx_gsp_msgq_rpc_transport_t rpc;
    uint8_t mmio_ready;
    uint8_t command_ready;
    uint8_t response_ready;
    uint8_t rpc_ready;
} nvx_gsp_ga10x_t;
/* Setup validates BAR coverage but performs no register writes. Zero-init out. */
nvx_gsp_ga10x_status_t nvx_gsp_ga10x_setup(
    nvx_gsp_ga10x_t *,const nvx_mmio_region_t *bar0);
/* Call before boot, with DMA-visible, page-aligned command backing memory. */
nvx_gsp_ga10x_status_t nvx_gsp_ga10x_create_command_queue(
    nvx_gsp_ga10x_t *,void *backing_store,size_t backing_size);
/* Call only after GSP has created its status-queue header; caller handles timeout/retry. */
nvx_gsp_ga10x_status_t nvx_gsp_ga10x_link_response_queue(
    nvx_gsp_ga10x_t *,void *backing_store,size_t backing_size);
/* Call after both queues are ready. */
nvx_gsp_ga10x_status_t nvx_gsp_ga10x_init_rpc(nvx_gsp_ga10x_t *);
#endif
