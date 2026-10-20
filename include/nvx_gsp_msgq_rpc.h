/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_MSGQ_RPC_H
#define VANTA_NVX_GSP_MSGQ_RPC_H
#include <stddef.h>
#include <stdint.h>
#include "nvx_gsp_msgq_queue.h"
#include "nvx_gsp_ring.h"
#define NVX_GSP_MSGQ_RPC_MAX_ELEMENTS 16U
typedef enum nvx_gsp_msgq_rpc_status {
    NVX_GSP_MSGQ_RPC_OK=0,
    NVX_GSP_MSGQ_RPC_INVALID_ARGUMENT,
    NVX_GSP_MSGQ_RPC_NOT_INITIALIZED,
    NVX_GSP_MSGQ_RPC_QUEUE_FULL,
    NVX_GSP_MSGQ_RPC_QUEUE_EMPTY,
    NVX_GSP_MSGQ_RPC_BAD_MESSAGE,
    NVX_GSP_MSGQ_RPC_BUFFER_TOO_SMALL,
    NVX_GSP_MSGQ_RPC_QUEUE_ERROR
} nvx_gsp_msgq_rpc_status_t;
typedef struct nvx_gsp_msgq_rpc_transport {
    nvx_gsp_msgq_queue_t *command_queue;
    nvx_gsp_msgq_queue_t *response_queue;
    uint32_t next_sequence;
    uint8_t initialized;
} nvx_gsp_msgq_rpc_transport_t;
/* Zero-initialize before init. This is the fixed-4096-byte-slot GSP RM transport. */
nvx_gsp_msgq_rpc_status_t nvx_gsp_msgq_rpc_init(
    nvx_gsp_msgq_rpc_transport_t *,nvx_gsp_msgq_queue_t *command_queue,
    nvx_gsp_msgq_queue_t *response_queue);
nvx_gsp_msgq_rpc_status_t nvx_gsp_msgq_rpc_send(
    nvx_gsp_msgq_rpc_transport_t *,uint32_t function,const void *payload,
    size_t payload_size,void *scratch,size_t scratch_size,uint32_t *out_sequence);
/* Returned payload points into scratch and remains valid only until scratch is reused. */
nvx_gsp_msgq_rpc_status_t nvx_gsp_msgq_rpc_receive(
    nvx_gsp_msgq_rpc_transport_t *,void *scratch,size_t scratch_size,
    nvx_gsp_rpc_view_t *out_message);
#endif
