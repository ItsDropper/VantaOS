/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9_device_runtime.h"
#include "intel_gen9.h"
#include "intel_gen9_ggtt.h"

#include <string.h>

#define PCI_COMMAND_OFFSET 0x04U
#define PCI_COMMAND_BUS_MASTER 0x0004U

static int enable_bus_master(intel_gen9_device_runtime_t *runtime)
{
    uint16_t command = pci_read_config16(runtime->pci_device,
                                        PCI_COMMAND_OFFSET);
    if (command == 0xFFFFU ||
        (command & PCI_COMMAND_BUS_MASTER) != 0U)
        return 0;
    /* Conservatively own cleanup even if config write readback fails. */
    runtime->bus_master_enabled_by_us = 1U;
    if (!pci_write_config16(runtime->pci_device, PCI_COMMAND_OFFSET,
            (uint16_t)(command | PCI_COMMAND_BUS_MASTER)))
        return 0;
    command = pci_read_config16(runtime->pci_device, PCI_COMMAND_OFFSET);
    return command != 0xFFFFU &&
           (command & PCI_COMMAND_BUS_MASTER) != 0U;
}

static int disable_bus_master(intel_gen9_device_runtime_t *runtime)
{
    uint16_t command;
    if (runtime->bus_master_enabled_by_us == 0U)
        return 1;
    command = pci_read_config16(runtime->pci_device, PCI_COMMAND_OFFSET);
    if (command == 0xFFFFU)
        return 0;
    command = (uint16_t)(command & (uint16_t)~PCI_COMMAND_BUS_MASTER);
    if (!pci_write_config16(runtime->pci_device, PCI_COMMAND_OFFSET, command))
        return 0;
    command = pci_read_config16(runtime->pci_device, PCI_COMMAND_OFFSET);
    if (command == 0xFFFFU ||
        (command & PCI_COMMAND_BUS_MASTER) != 0U)
        return 0;
    runtime->bus_master_enabled_by_us = 0U;
    return 1;
}

static int release_forcewake(intel_gen9_device_runtime_t *runtime)
{
    if (runtime->forcewake_held == 0U)
        return 1;
    if (!intel_gen9_forcewake_render_release(&runtime->mapping.region,
            INTEL_GEN9_DEVICE_FORCEWAKE_TIMEOUT))
        return 0;
    runtime->forcewake_held = 0U;
    return 1;
}

intel_gen9_device_status_t intel_gen9_device_runtime_init(
    intel_gen9_device_runtime_t *runtime, const struct pci_device *device,
    uint64_t lrca_ggtt_address, uint32_t context_id,
    uint16_t descriptor_flags, int context_ready, int device_quiesced)
{
    uint64_t descriptor = 0U;
    intel_gen9_execlist_status_t execlist_status;

    if (runtime == NULL || device == NULL || runtime->initialized != 0U ||
        runtime->mapping.active != 0U || runtime->execlist.initialized != 0U)
        return INTEL_GEN9_DEVICE_INVALID_ARGUMENT;
    if (!intel_gen9_match_id(device->vendor_id, device->device_id))
        return INTEL_GEN9_DEVICE_UNSUPPORTED;
    if (!context_ready || !device_quiesced)
        return INTEL_GEN9_DEVICE_NOT_READY;
    if (intel_gen9_execlist_build_descriptor(lrca_ggtt_address, context_id,
            descriptor_flags, &descriptor) != INTEL_GEN9_EXECLIST_OK)
        return INTEL_GEN9_DEVICE_INVALID_ARGUMENT;

    memset(runtime, 0, sizeof(*runtime));
    runtime->pci_device = device;
    runtime->context_id = context_id;
    runtime->context_descriptor = descriptor;

    /*
     * GTTMMADR is BAR0 on the supported Skylake Gen9 profile. BAR sizing and
     * mapping happen while PCI bus mastering is still disabled.
     */
    if (gpu_pci_mmio_map(device, INTEL_GEN9_GTTMMADR_BAR,
            INTEL_GEN9_GTTMMADR_SIZE, INTEL_GEN9_GTTMMADR_SIZE,
            &runtime->mapping) != GPU_PCI_MMIO_OK) {
        memset(runtime, 0, sizeof(*runtime));
        return INTEL_GEN9_DEVICE_MMIO_FAILED;
    }

    if (!intel_gen9_forcewake_render_acquire(&runtime->mapping.region,
            INTEL_GEN9_DEVICE_FORCEWAKE_TIMEOUT)) {
        if (gpu_pci_mmio_unmap(&runtime->mapping) != GPU_PCI_MMIO_OK) {
            runtime->initialized = 1U;
            return INTEL_GEN9_DEVICE_MMIO_FAILED;
        }
        memset(runtime, 0, sizeof(*runtime));
        return INTEL_GEN9_DEVICE_FORCEWAKE_FAILED;
    }
    runtime->forcewake_held = 1U;

    execlist_status = intel_gen9_execlist_init(&runtime->execlist,
        &runtime->mapping.region, 1, 1);
    if (execlist_status != INTEL_GEN9_EXECLIST_OK) {
        if (!release_forcewake(runtime) ||
            gpu_pci_mmio_unmap(&runtime->mapping) != GPU_PCI_MMIO_OK) {
            runtime->initialized = 1U;
            return INTEL_GEN9_DEVICE_SHUTDOWN_FAILED;
        }
        memset(runtime, 0, sizeof(*runtime));
        return INTEL_GEN9_DEVICE_MMIO_FAILED;
    }

    if (!enable_bus_master(runtime)) {
        /*
         * A failed readback may still mean the command bit reached hardware.
         * Keep the mapping alive if clearing it cannot be verified.
         */
        if (!disable_bus_master(runtime) || !release_forcewake(runtime)) {
            runtime->initialized = 1U;
            return INTEL_GEN9_DEVICE_PCI_FAILED;
        }
        (void)intel_gen9_execlist_shutdown(&runtime->execlist, 1);
        if (gpu_pci_mmio_unmap(&runtime->mapping) != GPU_PCI_MMIO_OK) {
            runtime->initialized = 1U;
            return INTEL_GEN9_DEVICE_PCI_FAILED;
        }
        memset(runtime, 0, sizeof(*runtime));
        return INTEL_GEN9_DEVICE_PCI_FAILED;
    }

    runtime->initialized = 1U;
    return INTEL_GEN9_DEVICE_OK;
}

intel_gen9_device_status_t intel_gen9_device_submit(
    intel_gen9_device_runtime_t *runtime)
{
    intel_gen9_execlist_status_t status;

    if (runtime == NULL || runtime->initialized == 0U ||
        runtime->forcewake_held == 0U)
        return INTEL_GEN9_DEVICE_INVALID_ARGUMENT;
    if (runtime->faulted != 0U)
        return INTEL_GEN9_DEVICE_NOT_READY;

    status = intel_gen9_execlist_submit(&runtime->execlist,
        runtime->context_descriptor, 1);
    return status == INTEL_GEN9_EXECLIST_OK ? INTEL_GEN9_DEVICE_OK :
                                              INTEL_GEN9_DEVICE_SUBMISSION_FAILED;
}

intel_gen9_device_status_t intel_gen9_device_wait(
    intel_gen9_device_runtime_t *runtime, unsigned timeout_iterations)
{
    if (runtime == NULL || runtime->initialized == 0U ||
        runtime->forcewake_held == 0U || timeout_iterations == 0U)
        return INTEL_GEN9_DEVICE_INVALID_ARGUMENT;
    if (runtime->faulted != 0U)
        return INTEL_GEN9_DEVICE_NOT_READY;

    for (unsigned i = 0U; i < timeout_iterations; ++i) {
        uint32_t context_id = 0U;
        uint32_t status = 0U;
        intel_gen9_execlist_status_t result =
            intel_gen9_execlist_poll_csb(&runtime->execlist,
                                         &context_id, &status);
        if (result == INTEL_GEN9_EXECLIST_OK) {
            if (context_id != runtime->context_id) {
                runtime->faulted = 1U;
                return INTEL_GEN9_DEVICE_SUBMISSION_FAILED;
            }
            return INTEL_GEN9_DEVICE_OK;
        }
        if (result != INTEL_GEN9_EXECLIST_BUSY) {
            runtime->faulted = 1U;
            return INTEL_GEN9_DEVICE_SUBMISSION_FAILED;
        }
    }

    runtime->faulted = 1U;
    return INTEL_GEN9_DEVICE_COMPLETION_TIMEOUT;
}

intel_gen9_device_status_t intel_gen9_device_runtime_shutdown(
    intel_gen9_device_runtime_t *runtime, int device_quiesced)
{
    if (runtime == NULL || runtime->initialized == 0U ||
        runtime->pci_device == NULL)
        return INTEL_GEN9_DEVICE_INVALID_ARGUMENT;
    if (!device_quiesced)
        return INTEL_GEN9_DEVICE_NOT_READY;

    if (runtime->execlist.initialized != 0U) {
        if (runtime->execlist.pending != 0U &&
            intel_gen9_execlist_abort_pending(&runtime->execlist, 1) !=
                INTEL_GEN9_EXECLIST_OK)
            return INTEL_GEN9_DEVICE_SHUTDOWN_FAILED;
        if (intel_gen9_execlist_shutdown(&runtime->execlist, 1) !=
                INTEL_GEN9_EXECLIST_OK)
            return INTEL_GEN9_DEVICE_SHUTDOWN_FAILED;
    }
    if (!release_forcewake(runtime))
        return INTEL_GEN9_DEVICE_SHUTDOWN_FAILED;
    if (!disable_bus_master(runtime))
        return INTEL_GEN9_DEVICE_SHUTDOWN_FAILED;
    if (runtime->mapping.active != 0U &&
        gpu_pci_mmio_unmap(&runtime->mapping) != GPU_PCI_MMIO_OK)
        return INTEL_GEN9_DEVICE_SHUTDOWN_FAILED;

    memset(runtime, 0, sizeof(*runtime));
    return INTEL_GEN9_DEVICE_OK;
}
