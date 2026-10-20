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
#define NVX_PCI_COMMAND_IO_SPACE       (1U << 0)
#define NVX_PCI_COMMAND_MEMORY_SPACE   (1U << 1)
#define NVX_PCI_COMMAND_BUS_MASTER     (1U << 2)

typedef enum nvx_generation_family {
    NVX_FAMILY_UNKNOWN = 0,
    NVX_FAMILY_LEGACY,
    NVX_FAMILY_TURING,
    NVX_FAMILY_AMPERE,
    NVX_FAMILY_ADA,
    NVX_FAMILY_HOPPER,
    NVX_FAMILY_BLACKWELL,
    NVX_FAMILY_RUBIN
} nvx_generation_family_t;

typedef enum nvx_device_profile {
    NVX_PROFILE_UNKNOWN = 0,
    NVX_PROFILE_GEFORCE_RTX_3060
} nvx_device_profile_t;

typedef struct nvx_device_descriptor {
    gpu_device_identity_t identity;
    nvx_generation_family_t family;
    nvx_device_profile_t profile;
    uint16_t pci_command;
    uint32_t architecture_id;
    uint32_t implementation_id;
    uint64_t mmio_base;
    uint64_t mmio_length;
    uint8_t is_64bit_bar;
} nvx_device_descriptor_t;

typedef struct nvx_device nvx_device_t;

int nvx_match_pci_device(const struct pci_device *pci_device);
int nvx_identify_pci_device(uint16_t device_id,
                            nvx_generation_family_t *out_family,
                            nvx_device_profile_t *out_profile);
/* Maps public PMC architecture codes; unknown codes are rejected, never guessed. */
int nvx_identify_architecture(uint8_t architecture,
                              nvx_generation_family_t *out_family);
int nvx_describe_pci_device(const struct pci_device *pci_device,
                            nvx_device_descriptor_t *out_descriptor);
int nvx_pci_discover(void);

#endif
