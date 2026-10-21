/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GENERATION_H
#define VANTA_NVX_GENERATION_H
#include <stddef.h>
#include <stdint.h>
#include "nvx_device.h"

#define NVX_GENERATION_CHIP_MAX 8U
#define NVX_GENERATION_FIRMWARE_COMPONENTS 5U

typedef enum {
    NVX_BOOT_SEQUENCE_UNSUPPORTED = 0,
    NVX_BOOT_SEQUENCE_GA10X_SEC2_GSP = 1
} nvx_boot_sequence_t;

typedef struct {
    nvx_generation_family_t family;
    nvx_device_profile_t profile;
    const uint16_t *pci_device_ids;
    size_t pci_device_id_count;
    const char *const *firmware_chips;
    size_t firmware_chip_count;
    const char *firmware_directory;
    const char *const *firmware_components;
    size_t firmware_component_count;
    nvx_boot_sequence_t boot_sequence;
    uint32_t pmc_boot_0_offset;
    uint32_t pmc_boot_1_offset;
    uint32_t pmc_boot_42_offset;
    uint32_t gsp_mailbox_base;
    uint32_t gsp_queue_head_base;
    uint32_t gsp_queue_tail_base;
} nvx_generation_descriptor_t;

/* Unknown IDs/chips return null; this lookup never maps MMIO or initializes hardware. */
const nvx_generation_descriptor_t *nvx_generation_for_pci(uint16_t device_id);
int nvx_generation_supports_firmware_chip(const char *chip);
#endif
