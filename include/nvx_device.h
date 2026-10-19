/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_DEVICE_H
#define VANTA_NVX_DEVICE_H

#include "gpu.h"
#include "pci.h"

#define NVX_PCI_VENDOR_NVIDIA 0x10DEu

typedef enum nvx_generation_family {
    NVX_FAMILY_UNKNOWN = 0,
    NVX_FAMILY_LEGACY,
    NVX_FAMILY_TURING,
    NVX_FAMILY_AMPERE,
    NVX_FAMILY_ADA,
    NVX_FAMILY_HOPPER
} nvx_generation_family_t;

typedef enum nvx_device_profile {
    NVX_PROFILE_UNKNOWN = 0,
    NVX_PROFILE_GEFORCE_RTX_3060
} nvx_device_profile_t;

typedef struct nvx_device_descriptor {
    gpu_device_identity_t identity;
    nvx_generation_family_t family;
    nvx_device_profile_t profile;
    uint32_t architecture_id;
    uint32_t implementation_id;
    uint64_t mmio_base;
    uint64_t mmio_length;
    uint8_t is_64bit_bar;
} nvx_device_descriptor_t;

typedef struct nvx_device nvx_device_t;

/* PCI match means candidate discovery only, not functional GPU support. */
int nvx_match_pci_device(const struct pci_device *pci_device);
/* Exact PCI-ID recognition only; unknown IDs must remain UNKNOWN. */
int nvx_identify_pci_device(uint16_t device_id,
                            nvx_generation_family_t *out_family,
                            nvx_device_profile_t *out_profile);
int nvx_describe_pci_device(const struct pci_device *pci_device,
                            nvx_device_descriptor_t *out_descriptor);
/* Read-only inventory of NVIDIA PCI functions; returns the detected count. */
int nvx_pci_discover(void);

#endif
