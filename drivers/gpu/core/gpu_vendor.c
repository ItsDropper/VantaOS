/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_vendor.h"

#include <stddef.h>

typedef struct gpu_id_entry {
    uint16_t vendor_id;
    uint16_t device_id;
    gpu_architecture_t architecture;
} gpu_id_entry_t;

/*
 * Deliberately curated IDs, not guessed numeric ranges. Unknown devices from
 * a supported vendor remain vendor-classified but architecture-unknown.
 */
static const gpu_id_entry_t known_ids[] = {
    { GPU_PCI_VENDOR_INTEL, 0x1912U, GPU_ARCH_INTEL_GEN9 },
    { GPU_PCI_VENDOR_INTEL, 0x9A49U, GPU_ARCH_INTEL_GEN11 },
    { GPU_PCI_VENDOR_INTEL, 0x9A40U, GPU_ARCH_INTEL_GEN11 },
    { GPU_PCI_VENDOR_INTEL, 0x46A6U, GPU_ARCH_INTEL_XE_LP },
    { GPU_PCI_VENDOR_INTEL, 0x56A0U, GPU_ARCH_INTEL_XE_HPG },
    { GPU_PCI_VENDOR_INTEL, 0x56A1U, GPU_ARCH_INTEL_XE_HPG },
    { GPU_PCI_VENDOR_INTEL, 0x7D55U, GPU_ARCH_INTEL_XE_LPG },
    { GPU_PCI_VENDOR_INTEL, 0xA780U, GPU_ARCH_INTEL_XE_LP },
    { GPU_PCI_VENDOR_AMD, 0x67DFU, GPU_ARCH_AMD_GCN4 },
    { GPU_PCI_VENDOR_AMD, 0x731FU, GPU_ARCH_AMD_RDNA1 },
    { GPU_PCI_VENDOR_AMD, 0x73BFU, GPU_ARCH_AMD_RDNA2 },
    { GPU_PCI_VENDOR_AMD, 0x744CU, GPU_ARCH_AMD_RDNA3 },
    { GPU_PCI_VENDOR_AMD, 0x15BFU, GPU_ARCH_AMD_RDNA_INTEGRATED },
    { GPU_PCI_VENDOR_AMD, 0x164EU, GPU_ARCH_AMD_RDNA_INTEGRATED }
};

static gpu_architecture_t gpu_lookup_architecture(uint16_t vendor_id,
                                                   uint16_t device_id)
{
    for (size_t i = 0; i < sizeof(known_ids) / sizeof(known_ids[0]); ++i) {
        if (known_ids[i].vendor_id == vendor_id &&
            known_ids[i].device_id == device_id)
            return known_ids[i].architecture;
    }
    return GPU_ARCH_UNKNOWN;
}

int gpu_classify_pci_function(const struct pci_device *device,
                              gpu_pci_match_t *out_match)
{
    gpu_pci_match_t result = {0};

    if (device == NULL || out_match == NULL)
        return 0;

    if (device->vendor_id == GPU_PCI_VENDOR_INTEL)
        result.vendor = GPU_VENDOR_INTEL;
    else if (device->vendor_id == GPU_PCI_VENDOR_AMD)
        result.vendor = GPU_VENDOR_AMD;
    else if (device->vendor_id == GPU_PCI_VENDOR_NVIDIA)
        result.vendor = GPU_VENDOR_NVIDIA;
    else
        return 0;

    if (device->class_code != GPU_PCI_CLASS_DISPLAY ||
        (device->subclass != GPU_PCI_SUBCLASS_VGA &&
         device->subclass != GPU_PCI_SUBCLASS_3D &&
         device->subclass != GPU_PCI_SUBCLASS_DISPLAY_OTHER))
        return 0;

    result.vendor_id = device->vendor_id;
    result.device_id = device->device_id;
    result.is_display_function = 1U;
    result.architecture = gpu_lookup_architecture(device->vendor_id,
                                                   device->device_id);
    result.is_known_device_id = result.architecture != GPU_ARCH_UNKNOWN;
    *out_match = result;
    return 1;
}

const char *gpu_vendor_name(gpu_vendor_t vendor)
{
    switch (vendor) {
        case GPU_VENDOR_INTEL: return "Intel";
        case GPU_VENDOR_AMD: return "AMD";
        case GPU_VENDOR_NVIDIA: return "NVIDIA";
        case GPU_VENDOR_UNKNOWN:
        default: return "Unknown";
    }
}

const char *gpu_architecture_name(gpu_architecture_t architecture)
{
    switch (architecture) {
        case GPU_ARCH_INTEL_GEN9: return "Intel Gen9";
        case GPU_ARCH_INTEL_GEN11: return "Intel Gen11";
        case GPU_ARCH_INTEL_XE_LP: return "Intel Xe-LP";
        case GPU_ARCH_INTEL_XE_HPG: return "Intel Xe-HPG";
        case GPU_ARCH_INTEL_XE_LPG: return "Intel Xe-LPG";
        case GPU_ARCH_AMD_GCN4: return "AMD GCN 4";
        case GPU_ARCH_AMD_RDNA1: return "AMD RDNA 1";
        case GPU_ARCH_AMD_RDNA2: return "AMD RDNA 2";
        case GPU_ARCH_AMD_RDNA3: return "AMD RDNA 3";
        case GPU_ARCH_AMD_RDNA_INTEGRATED: return "AMD RDNA integrated";
        case GPU_ARCH_NVIDIA_TURING: return "NVIDIA Turing";
        case GPU_ARCH_NVIDIA_AMPERE: return "NVIDIA Ampere";
        case GPU_ARCH_NVIDIA_ADA: return "NVIDIA Ada";
        case GPU_ARCH_UNKNOWN:
        default: return "Unknown";
    }
}
