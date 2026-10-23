/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_AMD_POLARIS_VM_HW_H
#define VANTA_AMD_POLARIS_VM_HW_H

#include "gpu_mmio.h"

#define AMD_POLARIS_VM_VA_BITS 36U
#define AMD_POLARIS_VM_DEFAULT_RANGE_SIZE (1ULL << AMD_POLARIS_VM_VA_BITS)
#define AMD_POLARIS_VM_CONTEXT1_ID 1U

typedef enum amd_polaris_vm_hw_status {
    AMD_POLARIS_VM_HW_OK = 0,
    AMD_POLARIS_VM_HW_INVALID_ARGUMENT,
    AMD_POLARIS_VM_HW_MMIO_ERROR,
    AMD_POLARIS_VM_HW_INVALID_ADDRESS,
    AMD_POLARIS_VM_HW_DEVICE_BUSY,
    AMD_POLARIS_VM_HW_VERIFY_FAILED,
    AMD_POLARIS_VM_HW_INVALIDATE_TIMEOUT
} amd_polaris_vm_hw_status_t;

/*
 * Programs the GFX8/Polaris VMID-1 root and range after the caller has
 * exclusively quiesced the GPU. fault_page_physical must be a valid DMA page.
 * This does not initialize PSP/SDMA/GFX firmware or enable command submission.
 */
amd_polaris_vm_hw_status_t amd_polaris_vm_install_root(
    const gpu_mmio_region_t *mmio, uint64_t root_physical,
    uint64_t va_start, uint64_t va_size, uint64_t fault_page_physical,
    unsigned timeout_iterations, int device_quiesced);

/* Request and boundedly wait for VMID-1 TLB invalidation. */
amd_polaris_vm_hw_status_t amd_polaris_vm_invalidate(
    const gpu_mmio_region_t *mmio, unsigned timeout_iterations);

#endif
