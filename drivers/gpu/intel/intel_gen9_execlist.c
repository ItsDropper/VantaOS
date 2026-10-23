/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9_execlist.h"

#include <string.h>

static int descriptor_valid(uint64_t descriptor, uint32_t *id)
{
    uint64_t lrca = descriptor & INTEL_GEN9_CONTEXT_DESCRIPTOR_LRCA_MASK;
    uint32_t context_id = (uint32_t)(
        (descriptor & INTEL_GEN9_CONTEXT_DESCRIPTOR_ID_MASK) >> 32U);

    if ((descriptor & INTEL_GEN9_CONTEXT_DESCRIPTOR_VALID) == 0U ||
        lrca == 0U || context_id == 0U ||
        (descriptor & INTEL_GEN9_CONTEXT_DESCRIPTOR_RESERVED_MASK) != 0U)
        return 0;
    *id = context_id;
    return 1;
}

intel_gen9_execlist_status_t intel_gen9_execlist_build_descriptor(
    uint64_t lrca_ggtt_address, uint32_t context_id, uint16_t flags,
    uint64_t *out_descriptor)
{
    if (out_descriptor == NULL || lrca_ggtt_address == 0U ||
        (lrca_ggtt_address & 0xFFFU) != 0U ||
        lrca_ggtt_address > INTEL_GEN9_CONTEXT_DESCRIPTOR_LRCA_MASK ||
        context_id == 0U || context_id > 0x1FFFFFU ||
        (flags & INTEL_GEN9_CONTEXT_DESCRIPTOR_VALID) == 0U ||
        (((uint64_t)flags) & ~INTEL_GEN9_CONTEXT_DESCRIPTOR_ALLOWED_FLAGS) != 0U)
        return INTEL_GEN9_EXECLIST_INVALID_DESCRIPTOR;

    *out_descriptor = lrca_ggtt_address |
                      ((uint64_t)context_id << 32U) | flags;
    return INTEL_GEN9_EXECLIST_OK;
}

intel_gen9_execlist_status_t intel_gen9_execlist_init(
    intel_gen9_execlist_t *state, const gpu_mmio_region_t *mmio,
    int render_forcewake_held, int engine_quiesced)
{
    if (state == NULL || mmio == NULL || mmio->base == NULL ||
        mmio->length < INTEL_GEN9_EXECLIST_MIN_MMIO_BYTES ||
        state->initialized != 0U)
        return INTEL_GEN9_EXECLIST_INVALID_ARGUMENT;
    if (!render_forcewake_held || !engine_quiesced)
        return INTEL_GEN9_EXECLIST_NOT_READY;
    memset(state, 0, sizeof(*state));
    state->mmio = mmio;
    state->initialized = 1U;
    return INTEL_GEN9_EXECLIST_OK;
}

intel_gen9_execlist_status_t intel_gen9_execlist_submit(
    intel_gen9_execlist_t *state, uint64_t context_descriptor,
    int port_available)
{
    uint32_t id;
    const size_t port = INTEL_GEN9_EXECLIST_ELSP_OFFSET;

    if (state == NULL || state->initialized == 0U || state->mmio == NULL)
        return INTEL_GEN9_EXECLIST_INVALID_ARGUMENT;
    if (state->faulted != 0U)
        return INTEL_GEN9_EXECLIST_NOT_READY;
    if (!descriptor_valid(context_descriptor, &id))
        return INTEL_GEN9_EXECLIST_INVALID_DESCRIPTOR;
    if (!port_available || state->pending != 0U)
        return INTEL_GEN9_EXECLIST_BUSY;

    /*
     * Gen8/Gen9 ELSP receives high/low for slot 1, then high/low for slot 0.
     * Slot 1 is empty here; CSB bookkeeping must serialize subsequent submits.
     */
    __atomic_thread_fence(__ATOMIC_RELEASE);
    if (gpu_mmio_write32(state->mmio, port, 0U) != GPU_MMIO_OK ||
        gpu_mmio_write32(state->mmio, port, 0U) != GPU_MMIO_OK ||
        gpu_mmio_write32(state->mmio, port,
                         (uint32_t)(context_descriptor >> 32U)) != GPU_MMIO_OK ||
        gpu_mmio_write32(state->mmio, port,
                         (uint32_t)context_descriptor) != GPU_MMIO_OK) {
        state->faulted = 1U;
        return INTEL_GEN9_EXECLIST_MMIO_ERROR;
    }
    state->pending_context_id = id;
    state->pending = 1U;
    return INTEL_GEN9_EXECLIST_OK;
}

intel_gen9_execlist_status_t intel_gen9_execlist_complete(
    intel_gen9_execlist_t *state, uint32_t completed_context_id)
{
    if (state == NULL || state->initialized == 0U)
        return INTEL_GEN9_EXECLIST_INVALID_ARGUMENT;
    if (state->faulted != 0U)
        return INTEL_GEN9_EXECLIST_NOT_READY;
    if (state->pending == 0U ||
        completed_context_id != state->pending_context_id)
        return INTEL_GEN9_EXECLIST_STALE_COMPLETION;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    state->pending = 0U;
    state->pending_context_id = 0U;
    return INTEL_GEN9_EXECLIST_OK;
}

intel_gen9_execlist_status_t intel_gen9_execlist_shutdown(
    intel_gen9_execlist_t *state, int engine_quiesced)
{
    if (state == NULL || state->initialized == 0U)
        return INTEL_GEN9_EXECLIST_INVALID_ARGUMENT;
    if (!engine_quiesced || state->pending != 0U)
        return INTEL_GEN9_EXECLIST_NOT_READY;
    memset(state, 0, sizeof(*state));
    return INTEL_GEN9_EXECLIST_OK;
}
