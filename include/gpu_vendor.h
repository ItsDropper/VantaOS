/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_VENDOR_H
#define VANTA_GPU_VENDOR_H

#include <stdint.h>
#include "pci.h"

#define GPU_PCI_VENDOR_INTEL 0x8086U
#define GPU_PCI_VENDOR_AMD   0x1002U
#define GPU_PCI_VENDOR_NVIDIA 0x10DEU
#define GPU_PCI_CLASS_DISPLAY 0x03U
#define GPU_PCI_SUBCLASS_VGA 0x00U
#define GPU_PCI_SUBCLASS_3D 0x02U
#define GPU_PCI_SUBCLASS_DISPLAY_OTHER 0x80U

typedef enum gpu_vendor {
    GPU_VENDOR_UNKNOWN = 0,
    GPU_VENDOR_INTEL,
    GPU_VENDOR_AMD,
    GPU_VENDOR_NVIDIA
} gpu_vendor_t;

typedef enum gpu_architecture {
    GPU_ARCH_UNKNOWN = 0,
    GPU_ARCH_INTEL_GEN9,
    GPU_ARCH_INTEL_GEN11,
    GPU_ARCH_INTEL_XE_LP,
    GPU_ARCH_INTEL_XE_HPG,
    GPU_ARCH_INTEL_XE_LPG,
    GPU_ARCH_AMD_GCN4,
    GPU_ARCH_AMD_RDNA1,
    GPU_ARCH_AMD_RDNA2,
    GPU_ARCH_AMD_RDNA3,
    GPU_ARCH_AMD_RDNA_INTEGRATED,
    GPU_ARCH_NVIDIA_TURING,
    GPU_ARCH_NVIDIA_AMPERE,
    GPU_ARCH_NVIDIA_ADA
} gpu_architecture_t;

typedef struct gpu_pci_match {
    gpu_vendor_t vendor;
    gpu_architecture_t architecture;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t is_display_function;
    uint8_t is_known_device_id;
} gpu_pci_match_t;

/* Classification is read-only: it never maps BARs or enables PCI bus mastering. */
int gpu_classify_pci_function(const struct pci_device *device,
                              gpu_pci_match_t *out_match);
const char *gpu_vendor_name(gpu_vendor_t vendor);
const char *gpu_architecture_name(gpu_architecture_t architecture);

#endif
