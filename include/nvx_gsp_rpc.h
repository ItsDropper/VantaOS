/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_RPC_H
#define VANTA_NVX_GSP_RPC_H
#include <stddef.h>
#include <stdint.h>
#include "nvx_gsp_ring.h"
#include "nvx_gsp_msgq_rpc.h"
#define NVX_GSP_RPC_MAX_PENDING 32U
typedef struct nvx_gsp_rpc_handle { uint32_t generation; uint16_t slot; uint16_t valid; } nvx_gsp_rpc_handle_t;
typedef enum nvx_gsp_rpc_status {
    NVX_GSP_RPC_OK=0, NVX_GSP_RPC_INVALID_ARGUMENT, NVX_GSP_RPC_NOT_INITIALIZED,
    NVX_GSP_RPC_NO_SLOTS, NVX_GSP_RPC_SEND_FAILED, NVX_GSP_RPC_STALE_HANDLE,
    NVX_GSP_RPC_PENDING, NVX_GSP_RPC_COMPLETE, NVX_GSP_RPC_FAILED,
    NVX_GSP_RPC_UNMATCHED_MESSAGE, NVX_GSP_RPC_DEVICE_LOST,
    NVX_GSP_RPC_FUNCTION_MISMATCH, NVX_GSP_RPC_RESPONSE_TOO_LARGE,
    NVX_GSP_RPC_BUSY, NVX_GSP_RPC_TRANSPORT_ERROR
} nvx_gsp_rpc_status_t;
typedef struct nvx_gsp_rpc_result {
    uint32_t sequence, function, firmware_result, firmware_result_private;
    size_t response_size;
    nvx_gsp_rpc_status_t status;
} nvx_gsp_rpc_result_t;
typedef struct nvx_gsp_rpc_slot {
    uint8_t *response_buffer;
    size_t response_capacity, response_size;
    uint32_t generation, sequence, request_function, response_function;
    uint32_t firmware_result, firmware_result_private;
    nvx_gsp_rpc_status_t state;
    uint8_t active;
} nvx_gsp_rpc_slot_t;
typedef enum nvx_gsp_rpc_transport_kind {
    NVX_GSP_RPC_TRANSPORT_NONE=0,
    NVX_GSP_RPC_TRANSPORT_RING,
    NVX_GSP_RPC_TRANSPORT_MSGQ
} nvx_gsp_rpc_transport_kind_t;
typedef struct nvx_gsp_rpc_manager {
    spinlock_t lock;
    nvx_gsp_ring_t *command_ring;
    nvx_gsp_ring_t *response_ring;
    nvx_gsp_msgq_rpc_transport_t *msgq_transport;
    nvx_gsp_rpc_transport_kind_t transport_kind;
    nvx_gsp_rpc_slot_t slots[NVX_GSP_RPC_MAX_PENDING];
    uint8_t initialized, device_lost;
} nvx_gsp_rpc_manager_t;
/* Zero-initialize manager. Response storage must remain valid until handle release. */
nvx_gsp_rpc_status_t nvx_gsp_rpc_init(nvx_gsp_rpc_manager_t *, nvx_gsp_ring_t *command_ring, nvx_gsp_ring_t *response_ring);
/* Same correlation/handle API over the hardware-oriented MSGQ transport. */
nvx_gsp_rpc_status_t nvx_gsp_rpc_init_msgq(nvx_gsp_rpc_manager_t *,
    nvx_gsp_msgq_rpc_transport_t *transport);
nvx_gsp_rpc_status_t nvx_gsp_rpc_submit(nvx_gsp_rpc_manager_t *,
    uint32_t header_version, uint32_t signature, uint32_t request_function,
    uint32_t response_function, const void *payload, size_t payload_size,
    void *scratch, size_t scratch_size, void *response_buffer,
    size_t response_capacity, nvx_gsp_rpc_handle_t *out_handle);
nvx_gsp_rpc_status_t nvx_gsp_rpc_pump(nvx_gsp_rpc_manager_t *,
    void *scratch, size_t scratch_size, nvx_gsp_rpc_view_t *out_unmatched);
nvx_gsp_rpc_status_t nvx_gsp_rpc_query(nvx_gsp_rpc_manager_t *,
    const nvx_gsp_rpc_handle_t *, nvx_gsp_rpc_result_t *out_result);
nvx_gsp_rpc_status_t nvx_gsp_rpc_release(nvx_gsp_rpc_manager_t *,
    nvx_gsp_rpc_handle_t *);
nvx_gsp_rpc_status_t nvx_gsp_rpc_mark_device_lost(nvx_gsp_rpc_manager_t *);
#endif
