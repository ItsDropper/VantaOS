/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_INIT_ARGS_H
#define VANTA_NVX_GSP_INIT_ARGS_H

#include <stddef.h>
#include <stdint.h>
#include "nvx_gsp_ring.h"

#define NVX_GSP_INIT_ARGS_MAGIC UINT32_C(0x20505347)
#define NVX_GSP_INIT_ARGS_PAGE_SIZE NVX_GSP_PAGE_SIZE
#define NVX_GSP_QUEUE_HEADER_SIZE_PLAIN 24U
#define NVX_GSP_QUEUE_ELEMENT_MIN_SIZE NVX_GSP_PAGE_SIZE
#define NVX_GSP_QUEUE_ELEMENT_MAX_SIZE (16U * NVX_GSP_PAGE_SIZE)
#define NVX_GSP_QUEUE_HEADER_ALIGNMENT 4U
#define NVX_GSP_QUEUE_ELEMENT_ALIGNMENT 12U

#define NVX_GSP_INIT_ARGS_FLAG_IN_PM_TRANSITION (UINT64_C(1) << 0U)
#define NVX_GSP_INIT_ARGS_FLAG_STACK_IN_DMEM (UINT64_C(1) << 1U)
#define NVX_GSP_INIT_ARGS_FLAG_RECOVERY_MARGIN_PRESENT (UINT64_C(1) << 2U)
#define NVX_GSP_INIT_ARGS_FLAG_SCAN_RECOVERY_MARGIN (UINT64_C(1) << 3U)

typedef enum nvx_gsp_init_args_status {
    NVX_GSP_INIT_ARGS_OK = 0,
    NVX_GSP_INIT_ARGS_INVALID_ARGUMENT,
    NVX_GSP_INIT_ARGS_BAD_ALIGNMENT,
    NVX_GSP_INIT_ARGS_BAD_QUEUE_SIZE,
    NVX_GSP_INIT_ARGS_SIZE_OVERFLOW,
    NVX_GSP_INIT_ARGS_INSUFFICIENT_MEMORY,
    NVX_GSP_INIT_ARGS_UNSUPPORTED_CONFIGURATION,
    NVX_GSP_INIT_ARGS_BAD_LAYOUT,
    NVX_GSP_INIT_ARGS_BAD_ARGUMENTS
} nvx_gsp_init_args_status_t;

/*
 * Modern GA10x GSP_ARGUMENTS_CACHED ABI from NVIDIA's current public
 * open-gpu-kernel-modules gsp_init_args.h. This is NOT the R535-era structure;
 * pair it only with a firmware branch whose init ABI matches. The separate
 * NVX_GSP_RPC_HEADER_VERSION_R535 constant describes the RPC header, not this
 * startup structure. Keep this a wire-format structure without native pointers.
 */
typedef struct nvx_gsp_modern_message_queue_init_args {
    uint64_t flags;
    uint64_t shared_mem_phys_addr;
    uint64_t command_queue_offset;
    uint64_t status_queue_offset;
    uint32_t queue_element_header_size;
    uint32_t queue_element_size_min;
    uint32_t queue_element_size_max;
    uint32_t queue_header_alignment;
    uint32_t queue_element_alignment;
    uint32_t page_table_entry_count;
    uint8_t reserved[8];
} nvx_gsp_modern_message_queue_init_args_t;

typedef struct nvx_gsp_sr_init_args {
    uint32_t old_level;
    uint32_t flags;
    uint8_t reserved[8];
} nvx_gsp_sr_init_args_t;

typedef struct nvx_gsp_phys_range {
    uint64_t address;
    uint64_t size;
} nvx_gsp_phys_range_t;

typedef struct nvx_gsp_bindata_range {
    uint64_t radix3;
    uint64_t size;
} nvx_gsp_bindata_range_t;

typedef struct nvx_gsp_arguments_cached_modern {
    uint32_t magic;
    uint16_t size;
    uint8_t reserved[2];
    uint64_t flags;
    nvx_gsp_modern_message_queue_init_args_t message_queue;
    nvx_gsp_sr_init_args_t sr;
    uint8_t reserved2[4];
    uint32_t gpu_instance;
    nvx_gsp_phys_range_t profiler_args;
    nvx_gsp_phys_range_t sysmem_heap_args;
    nvx_gsp_phys_range_t rm_state_monitor_buffer_args;
    nvx_gsp_bindata_range_t bindata_args;
    nvx_gsp_phys_range_t vbios_override_args;
} nvx_gsp_arguments_cached_modern_t;

typedef struct nvx_gsp_modern_shared_queue_config {
    uint64_t shared_mem_phys_addr;
    uint64_t allocation_size;
    uint64_t command_queue_size;
    uint64_t status_queue_size;
    uint32_t queue_element_header_size;
    uint32_t queue_element_size_min;
    uint32_t queue_element_size_max;
    uint32_t queue_header_alignment;
    uint32_t queue_element_alignment;
} nvx_gsp_modern_shared_queue_config_t;

typedef struct nvx_gsp_modern_shared_queue_layout {
    uint64_t shared_mem_phys_addr;
    uint64_t allocation_size;
    uint64_t required_size;
    uint64_t page_table_size;
    uint64_t command_queue_offset;
    uint64_t command_queue_size;
    uint64_t status_queue_offset;
    uint64_t status_queue_size;
    uint32_t page_table_entry_count;
    uint32_t queue_element_header_size;
    uint32_t queue_element_size_min;
    uint32_t queue_element_size_max;
    uint32_t queue_header_alignment;
    uint32_t queue_element_alignment;
} nvx_gsp_modern_shared_queue_layout_t;

/*
 * Computes the shared SYSRAM size and PTE count using NVIDIA's queue layout
 * formula. Queue sizes include each queue's header page and must be page-aligned.
 */
nvx_gsp_init_args_status_t nvx_gsp_modern_shared_queue_required_size(
    uint64_t command_queue_size, uint64_t status_queue_size,
    uint64_t *out_required_size, uint64_t *out_page_table_size,
    uint32_t *out_page_table_entry_count);

/* Plans a non-encrypted GA10x queue layout; no allocation or device I/O occurs. */
nvx_gsp_init_args_status_t nvx_gsp_modern_shared_queue_plan(
    const nvx_gsp_modern_shared_queue_config_t *config,
    nvx_gsp_modern_shared_queue_layout_t *out_layout);
nvx_gsp_init_args_status_t nvx_gsp_modern_shared_queue_validate(
    const nvx_gsp_modern_shared_queue_layout_t *layout);
/* Fill the self-mapping GSP bootloader PTE array at the start of shared memory. */
nvx_gsp_init_args_status_t nvx_gsp_modern_shared_queue_build_pte_table(
    const nvx_gsp_modern_shared_queue_layout_t *layout, void *shared_mem_cpu,
    size_t capacity);
/* Verify every PTE and require unused entries in the PTE page to remain zero. */
nvx_gsp_init_args_status_t nvx_gsp_modern_shared_queue_validate_pte_table(
    const nvx_gsp_modern_shared_queue_layout_t *layout,
    const void *shared_mem_cpu, size_t capacity);

/* Builds normal-boot arguments; only the explicit DMEM-stack flag is supported. */
nvx_gsp_init_args_status_t nvx_gsp_arguments_cached_modern_build(
    const nvx_gsp_modern_shared_queue_layout_t *layout, uint32_t gpu_instance,
    uint64_t flags, nvx_gsp_arguments_cached_modern_t *out_arguments);

/* Writes the ABI structure to a zero-filled, page-sized init-argument buffer. */
nvx_gsp_init_args_status_t nvx_gsp_arguments_cached_modern_materialize(
    const nvx_gsp_arguments_cached_modern_t *arguments, void *page, size_t capacity);

#endif
