/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9_device_runtime.h"
#include "intel_gen9.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint16_t pci_command;
static int mapped;
static int forcewake;
static int completion_ready;
static unsigned submit_count;
static uint32_t active_context;
static uint8_t active_pending;
static uint8_t active_faulted;
static uint8_t active_initialized;
static uint8_t unmap_count;

uint16_t pci_read_config16(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    return offset == 0x04U ? pci_command : 0U;
}

int pci_write_config16(const struct pci_device *device, uint8_t offset,
                       uint16_t value)
{
    (void)device;
    if (offset != 0x04U)
        return 0;
    pci_command = value;
    return 1;
}

int intel_gen9_match_id(uint16_t vendor_id, uint16_t device_id)
{
    return vendor_id == 0x8086U &&
           (device_id == 0x1912U || device_id == 0x1916U);
}

gpu_pci_mmio_status_t gpu_pci_mmio_map(
    const struct pci_device *device, uint8_t bar_index,
    size_t audited_bar_size, size_t map_length,
    gpu_pci_mmio_mapping_t *out_mapping)
{
    static uint8_t fake_mmio[16];
    if (device == NULL || bar_index != 0U ||
        audited_bar_size != INTEL_GEN9_GTTMMADR_SIZE ||
        map_length != INTEL_GEN9_GTTMMADR_SIZE ||
        out_mapping == NULL || (pci_command & 0x0004U) != 0U)
        return GPU_PCI_MMIO_RESOURCE_INVALID;

    memset(out_mapping, 0, sizeof(*out_mapping));
    out_mapping->device = device;
    out_mapping->mapping = fake_mmio;
    out_mapping->region.base = fake_mmio;
    out_mapping->region.length = map_length;
    out_mapping->physical_base = 0xE0000000ULL;
    out_mapping->mapped_length = map_length;
    out_mapping->original_command = pci_command;
    out_mapping->bar_index = 0U;
    out_mapping->enabled_memory_decode =
        (uint8_t)((pci_command & 0x0002U) == 0U);
    out_mapping->active = 1U;
    pci_command |= 0x0002U;
    mapped = 1;
    return GPU_PCI_MMIO_OK;
}

gpu_pci_mmio_status_t gpu_pci_mmio_unmap(gpu_pci_mmio_mapping_t *mapping)
{
    if (mapping == NULL || mapping->active == 0U || !mapped)
        return GPU_PCI_MMIO_INVALID_ARGUMENT;
    if (mapping->enabled_memory_decode != 0U)
        pci_command &= (uint16_t)~0x0002U;
    memset(mapping, 0, sizeof(*mapping));
    mapped = 0;
    ++unmap_count;
    return GPU_PCI_MMIO_OK;
}

int intel_gen9_forcewake_render_acquire(const gpu_mmio_region_t *mmio,
                                        unsigned timeout_iterations)
{
    if (mmio == NULL || mmio->base == NULL || timeout_iterations == 0U)
        return 0;
    forcewake = 1;
    return 1;
}

int intel_gen9_forcewake_render_release(const gpu_mmio_region_t *mmio,
                                        unsigned timeout_iterations)
{
    if (mmio == NULL || mmio->base == NULL || timeout_iterations == 0U ||
        !forcewake)
        return 0;
    forcewake = 0;
    return 1;
}

intel_gen9_execlist_status_t intel_gen9_execlist_build_descriptor(
    uint64_t lrca_ggtt_address, uint32_t context_id, uint16_t flags,
    uint64_t *out_descriptor)
{
    if (out_descriptor == NULL || lrca_ggtt_address == 0U ||
        (lrca_ggtt_address & 0xFFFU) != 0U ||
        context_id == 0U || context_id > 0x1FFFFFU ||
        (flags & 1U) == 0U || (flags & ~0x119U) != 0U)
        return INTEL_GEN9_EXECLIST_INVALID_DESCRIPTOR;
    *out_descriptor = lrca_ggtt_address |
                      ((uint64_t)context_id << 32U) | flags;
    return INTEL_GEN9_EXECLIST_OK;
}

intel_gen9_execlist_status_t intel_gen9_execlist_init(
    intel_gen9_execlist_t *state, const gpu_mmio_region_t *mmio,
    int render_forcewake_held, int engine_quiesced)
{
    if (state == NULL || mmio == NULL || !render_forcewake_held ||
        !engine_quiesced || state->initialized)
        return INTEL_GEN9_EXECLIST_INVALID_ARGUMENT;
    state->mmio = mmio;
    state->initialized = 1U;
    state->faulted = 0U;
    state->pending = 0U;
    active_initialized = 1U;
    active_faulted = 0U;
    active_pending = 0U;
    return INTEL_GEN9_EXECLIST_OK;
}

intel_gen9_execlist_status_t intel_gen9_execlist_submit(
    intel_gen9_execlist_t *state, uint64_t context_descriptor,
    int port_available)
{
    if (state == NULL || !state->initialized || !port_available ||
        state->pending || state->faulted)
        return INTEL_GEN9_EXECLIST_BUSY;
    active_context = (uint32_t)((context_descriptor >> 32U) & 0x1FFFFFU);
    state->pending_context_id = active_context;
    state->pending = 1U;
    active_pending = 1U;
    ++submit_count;
    return INTEL_GEN9_EXECLIST_OK;
}

intel_gen9_execlist_status_t intel_gen9_execlist_poll_csb(
    intel_gen9_execlist_t *state, uint32_t *out_context_id,
    uint32_t *out_status)
{
    if (state == NULL || out_context_id == NULL || out_status == NULL ||
        !state->initialized)
        return INTEL_GEN9_EXECLIST_INVALID_ARGUMENT;
    *out_context_id = 0U;
    *out_status = 0U;
    if (!completion_ready || !state->pending)
        return INTEL_GEN9_EXECLIST_BUSY;
    *out_context_id = active_context;
    *out_status = INTEL_GEN9_CSB_STATUS_COMPLETE;
    state->pending = 0U;
    state->pending_context_id = 0U;
    active_pending = 0U;
    return INTEL_GEN9_EXECLIST_OK;
}

intel_gen9_execlist_status_t intel_gen9_execlist_abort_pending(
    intel_gen9_execlist_t *state, int engine_quiesced)
{
    if (state == NULL || !state->initialized || !engine_quiesced)
        return INTEL_GEN9_EXECLIST_NOT_READY;
    state->pending = 0U;
    state->pending_context_id = 0U;
    state->faulted = 1U;
    active_pending = 0U;
    active_faulted = 1U;
    return INTEL_GEN9_EXECLIST_OK;
}

intel_gen9_execlist_status_t intel_gen9_execlist_shutdown(
    intel_gen9_execlist_t *state, int engine_quiesced)
{
    if (state == NULL || !state->initialized || !engine_quiesced ||
        state->pending)
        return INTEL_GEN9_EXECLIST_NOT_READY;
    memset(state, 0, sizeof(*state));
    active_initialized = 0U;
    return INTEL_GEN9_EXECLIST_OK;
}

static struct pci_device make_device(uint16_t device_id)
{
    struct pci_device device = {
        .vendor_id = 0x8086U,
        .device_id = device_id,
        .class_code = 0x03U,
        .subclass = 0x00U
    };
    return device;
}

static void reset_mocks(void)
{
    pci_command = 0U;
    mapped = 0;
    forcewake = 0;
    completion_ready = 0;
    submit_count = 0U;
    active_context = 0U;
    active_pending = 0U;
    active_faulted = 0U;
    active_initialized = 0U;
    unmap_count = 0U;
}

static void test_invalid_preconditions(void)
{
    struct pci_device device = make_device(0x1912U);
    intel_gen9_device_runtime_t runtime = {0};

    reset_mocks();
    assert(intel_gen9_device_runtime_init(&runtime, &device, 0x1000U,
        1U, 0x9U, 0, 1) == INTEL_GEN9_DEVICE_NOT_READY);
    assert(!mapped && !forcewake && pci_command == 0U);
    device.device_id = 0xFFFFU;
    assert(intel_gen9_device_runtime_init(&runtime, &device, 0x1000U,
        1U, 0x9U, 1, 1) == INTEL_GEN9_DEVICE_UNSUPPORTED);
    assert(!mapped);
}

static void test_completion_and_teardown(void)
{
    struct pci_device device = make_device(0x1912U);
    intel_gen9_device_runtime_t runtime = {0};

    reset_mocks();
    assert(intel_gen9_device_runtime_init(&runtime, &device, 0x1000U,
        7U, 0x9U, 1, 1) == INTEL_GEN9_DEVICE_OK);
    assert(runtime.initialized && runtime.forcewake_held);
    assert((pci_command & 0x0006U) == 0x0006U);
    assert(intel_gen9_device_submit(&runtime) == INTEL_GEN9_DEVICE_OK);
    assert(submit_count == 1U && active_pending);
    assert(intel_gen9_device_submit(&runtime) ==
           INTEL_GEN9_DEVICE_SUBMISSION_FAILED);

    assert(intel_gen9_device_wait(&runtime, 2U) ==
           INTEL_GEN9_DEVICE_COMPLETION_TIMEOUT);
    assert(runtime.faulted && runtime.execlist.pending);
    assert(intel_gen9_device_runtime_shutdown(&runtime, 0) ==
           INTEL_GEN9_DEVICE_NOT_READY);
    assert(intel_gen9_device_runtime_shutdown(&runtime, 1) ==
           INTEL_GEN9_DEVICE_OK);
    assert(!runtime.initialized && !mapped && !forcewake);
    assert((pci_command & 0x0004U) == 0U && pci_command == 0U);
    assert(unmap_count == 1U && !active_initialized && !active_pending);
}

static void test_verified_completion(void)
{
    struct pci_device device = make_device(0x1916U);
    intel_gen9_device_runtime_t runtime = {0};

    reset_mocks();
    assert(intel_gen9_device_runtime_init(&runtime, &device, 0x2000U,
        9U, 0x9U, 1, 1) == INTEL_GEN9_DEVICE_OK);
    assert(intel_gen9_device_submit(&runtime) == INTEL_GEN9_DEVICE_OK);
    completion_ready = 1;
    assert(intel_gen9_device_wait(&runtime, 4U) == INTEL_GEN9_DEVICE_OK);
    assert(!runtime.execlist.pending && !runtime.faulted);
    assert(intel_gen9_device_runtime_shutdown(&runtime, 1) ==
           INTEL_GEN9_DEVICE_OK);
    assert(!mapped && !forcewake && unmap_count == 1U);
}

int main(void)
{
    test_invalid_preconditions();
    test_completion_and_teardown();
    test_verified_completion();
    return 0;
}
