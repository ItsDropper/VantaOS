/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_RING_H
#define VANTA_NVX_GSP_RING_H
#include <stddef.h>
#include <stdint.h>
#include "spinlock.h"
#define NVX_GSP_PAGE_SIZE 4096U
#define NVX_GSP_MSG_HEADER_SIZE 24U
#define NVX_GSP_RPC_HEADER_SIZE 32U
#define NVX_GSP_RPC_HEADER_VERSION_R535 0x03000000U
#define NVX_GSP_RPC_SIGNATURE_R535 0x43505256U
#define NVX_GSP_NVDM_TYPE_RM_RPC 0x25U
#define NVX_GSP_MCTP_MAGIC 0x4D435450U
#define NVX_GSP_MCTP_HEADER_SINGLE 0xC0000001U
#define NVX_GSP_NVDM_HEADER_RM_RPC 0x2510DE7EU
#define NVX_GSP_MAX_MESSAGE_PAGES 16U
#define NVX_GSP_MAX_QUEUE_PAGES 63U
#define NVX_GSP_MAX_RPC_PAYLOAD (NVX_GSP_MAX_MESSAGE_PAGES * NVX_GSP_PAGE_SIZE - NVX_GSP_MSG_HEADER_SIZE - NVX_GSP_RPC_HEADER_SIZE)
typedef enum nvx_gsp_ring_status {
    NVX_GSP_RING_OK=0, NVX_GSP_RING_INVALID_ARGUMENT,
    NVX_GSP_RING_NOT_INITIALIZED, NVX_GSP_RING_QUEUE_FULL,
    NVX_GSP_RING_QUEUE_EMPTY, NVX_GSP_RING_BAD_POINTER,
    NVX_GSP_RING_MESSAGE_TOO_LARGE, NVX_GSP_RING_BUFFER_TOO_SMALL,
    NVX_GSP_RING_BAD_MESSAGE
} nvx_gsp_ring_status_t;
typedef void (*nvx_gsp_ring_notify_t)(void *context);
typedef struct nvx_gsp_ring {
    uint8_t *storage;
    volatile uint32_t *read_ptr;
    volatile uint32_t *write_ptr;
    nvx_gsp_ring_notify_t notify;
    void *notify_context;
    spinlock_t lock;
    uint32_t next_sequence; /* Local API ticket; not part of NVIDIA's wire format. */
    uint32_t capacity_pages;
    uint8_t initialized;
} nvx_gsp_ring_t;
typedef struct nvx_gsp_rpc_view {
    uint32_t header_version, signature, length, function;
    uint32_t result, result_private, sequence, spare;
    const uint8_t *payload;
    size_t payload_size;
    uint32_t message_sequence, element_pages;
} nvx_gsp_rpc_view_t;
nvx_gsp_ring_status_t nvx_gsp_ring_init(nvx_gsp_ring_t *, void *storage,
    size_t capacity_pages, volatile uint32_t *read_ptr,
    volatile uint32_t *write_ptr, nvx_gsp_ring_notify_t, void *context);
nvx_gsp_ring_status_t nvx_gsp_ring_send(nvx_gsp_ring_t *, uint32_t header_version,
    uint32_t signature, uint32_t function, const void *payload, size_t payload_size,
    void *scratch, size_t scratch_size, uint32_t *out_sequence);
nvx_gsp_ring_status_t nvx_gsp_ring_receive(nvx_gsp_ring_t *, void *scratch,
    size_t scratch_size, nvx_gsp_rpc_view_t *out_message);
#endif
