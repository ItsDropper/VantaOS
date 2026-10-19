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
    NVX_FAMILY_UNKNOWN = 0, NVX_FAMILY_LEGACY, NVX_FAMILY_TURING,
    NVX_FAMILY_AMPERE, NVX_FAMILY_ADA, NVX_FAMILY_HOPPER
} nvx_generation_family_t;
typedef struct nvx_device_descriptor {
    gpu_device_identity_t identity;
    nvx_generation_family_t family;
    uint32_t architecture_id, implementation_id;
    uint64_t mmio_base, mmio_length;
    uint8_t is_64bit_bar;
} nvx_device_descriptor_t;
typedef struct nvx_device nvx_device_t;
/* PCI match means candidate discovery only, not functional GPU support. */
int nvx_match_pci_device(const struct pci_device *pci_device);
int nvx_describe_pci_device(const struct pci_device *pci_device,
                            nvx_device_descriptor_t *out_descriptor);
#endif
