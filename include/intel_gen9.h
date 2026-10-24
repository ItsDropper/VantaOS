/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_INTEL_GEN9_H
#define VANTA_INTEL_GEN9_H

#include <stdint.h>
#include "gpu_pci_resources.h"

/* Deliberately narrow initial target: Skylake Gen9 GT2 display functions. */
#define INTEL_GEN9_VENDOR_ID 0x8086U
#define INTEL_GEN9_SKL_GT2_DEVICE_ID 0x1912U
#define INTEL_GEN9_SKL_GT2_MOBILE_DEVICE_ID 0x1916U
#define INTEL_GEN9_GTTMMADR_BAR 0U

typedef enum intel_gen9_status {
    INTEL_GEN9_OK = 0,
    INTEL_GEN9_INVALID_ARGUMENT,
    INTEL_GEN9_UNSUPPORTED_DEVICE,
    INTEL_GEN9_NO_ASSIGNED_GTTMMADR,
    INTEL_GEN9_INVALID_GTTMMADR
} intel_gen9_status_t;

typedef struct intel_gen9_profile {
    uint16_t device_id;
    uint8_t generation;
    uint8_t requires_guc_firmware;
    uint8_t huc_firmware_media_only;
    uint8_t requires_gttmmadr;
    uint8_t acceleration_ready;
    uint64_t gttmmadr_base;
} intel_gen9_profile_t;

/* Exact IDs only; vendor-wide matches are not hardware support. */
int intel_gen9_match_id(uint16_t vendor_id, uint16_t device_id);
/* Read-only preflight. Does not map BARs, write registers, or enable DMA. */
intel_gen9_status_t intel_gen9_inspect(uint16_t vendor_id, uint16_t device_id,
                                       const gpu_pci_resource_inventory_t *resources,
                                       intel_gen9_profile_t *out_profile);

#endif
