/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_INTEL_GEN9_EXECLIST_H
#define VANTA_INTEL_GEN9_EXECLIST_H

#include <stdint.h>
#include "gpu_mmio.h"

#define INTEL_GEN9_EXECLIST_ELSP_OFFSET 0x2230U
#define INTEL_GEN9_EXECLIST_MIN_MMIO_BYTES (INTEL_GEN9_EXECLIST_ELSP_OFFSET + 4U)
#define INTEL_GEN9_CSB_BASE_OFFSET 0x2370U
#define INTEL_GEN9_CSB_PTR_OFFSET 0x23A0U
#define INTEL_GEN9_CSB_ENTRIES 6U
#define INTEL_GEN9_CSB_MIN_MMIO_BYTES (INTEL_GEN9_CSB_PTR_OFFSET + 4U)
#define INTEL_GEN9_CSB_STATUS_PREEMPTED (1U << 1U)
#define INTEL_GEN9_CSB_STATUS_ELEMENT_SWITCH (1U << 2U)
#define INTEL_GEN9_CSB_STATUS_ACTIVE_IDLE (1U << 3U)
#define INTEL_GEN9_CSB_STATUS_COMPLETE (1U << 4U)
#define INTEL_GEN9_CONTEXT_DESCRIPTOR_VALID (1ULL << 0)
#define INTEL_GEN9_CONTEXT_DESCRIPTOR_LRCA_MASK 0x00000000FFFFF000ULL
#define INTEL_GEN9_CONTEXT_DESCRIPTOR_ID_MASK 0x001FFFFF00000000ULL
#define INTEL_GEN9_CONTEXT_DESCRIPTOR_RESERVED_MASK 0xFFE0000000000000ULL
#define INTEL_GEN9_CONTEXT_DESCRIPTOR_ALLOWED_FLAGS 0x0000000000000119ULL

typedef enum intel_gen9_execlist_status {
    INTEL_GEN9_EXECLIST_OK = 0,
    INTEL_GEN9_EXECLIST_INVALID_ARGUMENT,
    INTEL_GEN9_EXECLIST_NOT_READY,
    INTEL_GEN9_EXECLIST_INVALID_DESCRIPTOR,
    INTEL_GEN9_EXECLIST_MMIO_ERROR,
    INTEL_GEN9_EXECLIST_VERIFY_FAILED,
    INTEL_GEN9_EXECLIST_BUSY,
    INTEL_GEN9_EXECLIST_STALE_COMPLETION
} intel_gen9_execlist_status_t;

/*
 * The context image/LRCA, ring tail, PPGTT, and fence batch must already be
 * initialized and visible to the GPU. The caller must hold render forcewake.
 * This module owns neither the context nor the command ring.
 */
typedef struct intel_gen9_execlist {
    const gpu_mmio_region_t *mmio;
    uint32_t pending_context_id;
    uint8_t csb_read_index;
    uint8_t csb_read_valid;
    uint8_t initialized;
    uint8_t pending;
    uint8_t faulted;
} intel_gen9_execlist_t;

/* Caller must zero-initialize this object before first use. */
intel_gen9_execlist_status_t intel_gen9_execlist_init(
    intel_gen9_execlist_t *execlist, const gpu_mmio_region_t *mmio,
    int render_forcewake_held, int engine_quiesced);

/* Build a descriptor for a 4 KiB-aligned LRCA in the 32-bit Gen9 GGTT. */
intel_gen9_execlist_status_t intel_gen9_execlist_build_descriptor(
    uint64_t lrca_ggtt_address, uint32_t context_id, uint16_t flags,
    uint64_t *out_descriptor);

/* Submit one valid Gen8/Gen9 LRCA descriptor through the ELSP port. */
intel_gen9_execlist_status_t intel_gen9_execlist_submit(
    intel_gen9_execlist_t *execlist, uint64_t context_descriptor,
    int port_available);

/*
 * Consume one Gen8/Gen9 context-status-buffer entry. A matching COMPLETE
 * event retires the pending descriptor; non-completion events are consumed
 * and reported as BUSY. Caller must hold render forcewake.
 */
intel_gen9_execlist_status_t intel_gen9_execlist_poll_csb(
    intel_gen9_execlist_t *execlist, uint32_t *out_context_id,
    uint32_t *out_status);

/* Call only after a verified CSB completion event for this context ID. */
intel_gen9_execlist_status_t intel_gen9_execlist_complete(
    intel_gen9_execlist_t *execlist, uint32_t completed_context_id);

/* Stop accepting work; this does not free the LRCA, ring, or status buffer. */
intel_gen9_execlist_status_t intel_gen9_execlist_shutdown(
    intel_gen9_execlist_t *execlist, int engine_quiesced);

#endif
