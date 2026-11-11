/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_vendor.h"

#include <assert.h>
#include <string.h>

static void check_supported(uint16_t vendor, uint16_t device_id,
                            gpu_architecture_t expected)
{
    struct pci_device device;
    gpu_pci_match_t match;

    memset(&device, 0, sizeof(device));
    device.vendor_id = vendor;
    device.device_id = device_id;
    device.class_code = GPU_PCI_CLASS_DISPLAY;
    device.subclass = GPU_PCI_SUBCLASS_VGA;

    assert(gpu_classify_pci_function(&device, &match));
    assert(match.vendor_id == vendor);
    assert(match.device_id == device_id);
    assert(match.is_display_function != 0U);
    assert(match.is_known_device_id != 0U);
    assert(match.architecture == expected);
}

static void check_not_supported(uint16_t vendor, uint16_t device_id)
{
    struct pci_device device;
    gpu_pci_match_t match;

    memset(&device, 0, sizeof(device));
    device.vendor_id = vendor;
    device.device_id = device_id;
    device.class_code = GPU_PCI_CLASS_DISPLAY;
    device.subclass = GPU_PCI_SUBCLASS_VGA;

    assert(gpu_classify_pci_function(&device, &match));
    assert(match.is_display_function != 0U);
    assert(match.is_known_device_id == 0U);
    assert(match.architecture == GPU_ARCH_UNKNOWN);
}

int main(void)
{
    check_supported(0x8086U, 0x1912U, GPU_ARCH_INTEL_GEN9);
    check_supported(0x8086U, 0x1916U, GPU_ARCH_INTEL_GEN9);
    check_supported(0x1002U, 0x67DFU, GPU_ARCH_AMD_GCN4);
    check_supported(0x1002U, 0x67C0U, GPU_ARCH_AMD_GCN4);

    /* Similar vendors and adjacent architectures must not be inferred. */
    check_not_supported(0x8086U, 0x1917U);
    check_not_supported(0x1002U, 0x67DEU);
    return 0;
}
