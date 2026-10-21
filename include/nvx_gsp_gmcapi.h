/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_GMCAPI_H
#define VANTA_NVX_GSP_GMCAPI_H

#include "nvx_gsp_msgq_queue.h"

#define NVX_GSP_GMCAPI_HEADER_SIZE 40U
#define NVX_GSP_GMCAPI_COMMAND_FLAG_RESPONSE UINT32_C(0x01000000)
#define NVX_GSP_GMCAPI_COMMAND_ID_MASK UINT32_C(0x00FFFFFF)
#define NVX_GSP_GMCAPI_NVDM_HEADER UINT32_C(0x2610DE7E)
#define NVX_GSP_GMCAPI_CMD_GSP_INIT UINT32_C(0x00010001)

typedef enum nvx_gsp_gmcapi_status {
    NVX_GSP_GMCAPI_OK = 0,
    NVX_GSP_GMCAPI_INVALID_ARGUMENT,
    NVX_GSP_GMCAPI_NOT_INITIALIZED,
    NVX_GSP_GMCAPI_QUEUE_ERROR,
    NVX_GSP_GMCAPI_TIMEOUT,
    NVX_GSP_GMCAPI_BAD_MESSAGE,
    NVX_GSP_GMCAPI_UNMATCHED_RESPONSE,
    NVX_GSP_GMCAPI_RESPONSE_TOO_LARGE,
    NVX_GSP_GMCAPI_REMOTE_ERROR,
    NVX_GSP_GMCAPI_BUSY
} nvx_gsp_gmcapi_status_t;

typedef void (*nvx_gsp_gmcapi_delay_fn)(uint32_t ms, void *context);

typedef struct nvx_gsp_gmcapi_transport {
    nvx_gsp_msgq_queue_t *command_queue;
    nvx_gsp_msgq_queue_t *status_queue;
    uint64_t next_sequence;
    uint8_t initialized;
    uint8_t busy;
} nvx_gsp_gmcapi_transport_t;

/* Zero-initialize transport before init; queues must already be hardware-linked. */
nvx_gsp_gmcapi_status_t nvx_gsp_gmcapi_init(
    nvx_gsp_gmcapi_transport_t *transport,
    nvx_gsp_msgq_queue_t *command_queue,
    nvx_gsp_msgq_queue_t *status_queue);

/*
 * Send one synchronous, unencrypted GMCAPI request and wait for its matching
 * response. Input/output are the command-specific payloads, not wire headers.
 * A valid encoded system-info payload is required for GSP_INIT; NVX does not
 * fabricate machine topology or firmware-specific system information.
 */
nvx_gsp_gmcapi_status_t nvx_gsp_gmcapi_call(
    nvx_gsp_gmcapi_transport_t *transport, uint32_t command_id,
    const void *request, size_t request_size,
    void *response, size_t response_capacity, size_t *out_response_size,
    uint32_t *out_remote_status,
    void *scratch, size_t scratch_size,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_gmcapi_delay_fn delay_ms, void *context);

nvx_gsp_gmcapi_status_t nvx_gsp_gmcapi_gsp_init(
    nvx_gsp_gmcapi_transport_t *transport,
    const void *encoded_system_info, size_t encoded_system_info_size,
    void *static_info, size_t static_info_capacity, size_t *out_static_info_size,
    uint32_t *out_remote_status,
    void *scratch, size_t scratch_size,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_gmcapi_delay_fn delay_ms, void *context);

#endif
