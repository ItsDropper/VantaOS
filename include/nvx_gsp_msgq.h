/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_MSGQ_H
#define VANTA_NVX_GSP_MSGQ_H
#include <stddef.h>
#include <stdint.h>
#define NVX_GSP_MSGQ_VERSION_MAJOR 2U
#define NVX_GSP_MSGQ_VERSION_MINOR 0U
#define NVX_GSP_MSGQ_HEADER_SIZE 32U
#define NVX_GSP_MSGQ_HEADER_PAGE_SIZE 4096U
#define NVX_GSP_MSGQ_ELEMENT_SIZE 4096U
#define NVX_GSP_MSGQ_MAX_ELEMENTS UINT32_MAX
typedef enum nvx_gsp_msgq_status {
    NVX_GSP_MSGQ_OK=0, NVX_GSP_MSGQ_INVALID_ARGUMENT,
    NVX_GSP_MSGQ_BAD_ALIGNMENT, NVX_GSP_MSGQ_BAD_SIZE,
    NVX_GSP_MSGQ_BAD_VERSION, NVX_GSP_MSGQ_BAD_LAYOUT,
    NVX_GSP_MSGQ_BAD_RESERVED
} nvx_gsp_msgq_status_t;
typedef struct nvx_gsp_msgq_layout {
    uint8_t *entries;
    uint32_t backing_store_size;
    uint32_t entry_offset;
    uint32_t element_size;
    uint32_t element_count;
} nvx_gsp_msgq_layout_t;
nvx_gsp_msgq_status_t nvx_gsp_msgq_prepare_tx(void *backing_store,
    size_t backing_store_size, size_t element_size, nvx_gsp_msgq_layout_t *out_layout);
nvx_gsp_msgq_status_t nvx_gsp_msgq_validate_rx(const void *backing_store,
    size_t backing_store_size, size_t expected_element_size,
    nvx_gsp_msgq_layout_t *out_layout);
#endif
