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
#include "nvx_gsp_falcon.h"
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
#define NVX_GSP_GA10X_REQUIRED_BAR_BYTES NVX_GSP_FALCON_DIAGNOSTIC_BYTES
/* GA102 scratch group 42: usable framebuffer size in MiB (Linux Nova). */
#define NVX_GSP_GA10X_USABLE_FB_SIZE_MB 0x001183A4U

/*
 * GA102+ FPF ucode-version fuse arrays (BAR0-relative), independently
 * cross-checked against Linux Nova and NVIDIA's GA102 open kernel module.
 * Each array has 16 32-bit slots; only bits 15:0 carry the fuse bitmap.
 */
#define NVX_GSP_GA10X_FUSE_NVDEC_UCODE1_VERSION 0x00824100U
#define NVX_GSP_GA10X_FUSE_SEC2_UCODE1_VERSION  0x00824140U
#define NVX_GSP_GA10X_FUSE_GSP_UCODE1_VERSION   0x008241C0U
#define NVX_GSP_GA10X_FUSE_UCODE_COUNT 16U
#define NVX_GSP_GA10X_ENGINE_MASK_SEC2 0x0001U
#define NVX_GSP_GA10X_ENGINE_MASK_NVDEC 0x0004U
#define NVX_GSP_GA10X_ENGINE_MASK_GSP 0x0400U
typedef enum nvx_gsp_ga10x_status {
    NVX_GSP_GA10X_OK=0,
    NVX_GSP_GA10X_INVALID_ARGUMENT,
    NVX_GSP_GA10X_BAD_BAR,
    NVX_GSP_GA10X_ALREADY_INITIALIZED,
    NVX_GSP_GA10X_QUEUE_NOT_READY,
    NVX_GSP_GA10X_QUEUE_ERROR,
    NVX_GSP_GA10X_PRIVILEGED_WINDOW_LOCKED,
    NVX_GSP_GA10X_MMIO_ERROR,
    NVX_GSP_GA10X_FUSE_UNSUPPORTED_ENGINE,
    NVX_GSP_GA10X_FUSE_INVALID_UCODE_ID,
    NVX_GSP_GA10X_FUSE_RESERVED_BITS,
    NVX_GSP_GA10X_FB_SIZE_INVALID
} nvx_gsp_ga10x_status_t;
typedef struct nvx_gsp_ga10x {
    nvx_gsp_msgq_mmio_context_t mmio_context;
    nvx_gsp_msgq_io_t io;
    nvx_gsp_msgq_queue_t command_queue;
    nvx_gsp_msgq_queue_t response_queue;
    nvx_gsp_msgq_rpc_transport_t rpc;
    nvx_gsp_falcon_snapshot_t falcon;
    uint8_t mmio_ready;
    uint8_t command_ready;
    uint8_t response_ready;
    uint8_t rpc_ready;
} nvx_gsp_ga10x_t;
/*
 * Read a hardware fuse bitmap for the exact firmware engine mask and 1-based
 * ucode ID. Returns the bitmap's bit length (highest set bit + 1), matching
 * NVIDIA's GA102 selection logic. It never guesses when the engine/ID is
 * unknown and requires the whole selected register to be inside the mapping.
 */
/* Read actual GA102 usable framebuffer size from the documented scratch register. */
nvx_gsp_ga10x_status_t nvx_gsp_ga10x_read_usable_fb_size(
    const nvx_mmio_region_t *bar0, uint64_t *out_bytes);
nvx_gsp_ga10x_status_t nvx_gsp_ga10x_read_ucode_fuse_version(
    const nvx_mmio_region_t *bar0, uint32_t engine_id_mask,
    uint32_t ucode_id, uint32_t *out_version);
/* Setup inspects GA10x Falcon readiness read-only and performs no register writes. Zero-init out. */
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
