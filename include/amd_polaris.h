/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_AMD_POLARIS_H
#define VANTA_AMD_POLARIS_H

#include <stdint.h>
#include "gpu_pci_resources.h"

/* Narrow initial target: Ellesmere/Polaris 10/20 discrete GPUs. */
#define AMD_POLARIS_VENDOR_ID 0x1002U
#define AMD_POLARIS_10_DEVICE_ID 0x67DFU
#define AMD_POLARIS_10_ALT_DEVICE_ID 0x67C0U
#define AMD_POLARIS_MMIO_BAR 2U

typedef enum amd_polaris_status {
    AMD_POLARIS_OK = 0,
    AMD_POLARIS_INVALID_ARGUMENT,
    AMD_POLARIS_UNSUPPORTED_DEVICE,
    AMD_POLARIS_NO_ASSIGNED_MMIO,
    AMD_POLARIS_INVALID_MMIO
} amd_polaris_status_t;

typedef struct amd_polaris_profile {
    uint16_t device_id;
    uint8_t architecture_gcn_major;
    uint8_t architecture_gcn_minor;
    uint8_t requires_psp_firmware;
    uint8_t requires_sdma_firmware;
    uint8_t requires_gfx_firmware;
    uint8_t acceleration_ready;
    uint64_t mmio_base;
} amd_polaris_profile_t;

/* Exact IDs only; this is not a blanket GCN/RDNA match. */
int amd_polaris_match_id(uint16_t vendor_id, uint16_t device_id);
/* Read-only preflight. Does not map BARs, write registers, or enable DMA. */
amd_polaris_status_t amd_polaris_inspect(
    uint16_t vendor_id, uint16_t device_id,
    const gpu_pci_resource_inventory_t *resources,
    amd_polaris_profile_t *out_profile);

#endif
