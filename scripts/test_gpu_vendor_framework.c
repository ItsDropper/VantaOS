/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_gpu.h"
#include "intel_gpu.h"

#include <assert.h>
#include <string.h>
#include <stddef.h>


static const struct pci_device inventory[] = {
    { .vendor_id = 0x8086U, .device_id = 0x56A0U, .class_code = 0x03U, .subclass = 0x00U },
    { .vendor_id = 0x1002U, .device_id = 0x744CU, .class_code = 0x03U, .subclass = 0x00U },
    { .vendor_id = 0x10DEU, .device_id = 0x2503U, .class_code = 0x03U, .subclass = 0x00U },
    { .vendor_id = 0x8086U, .device_id = 0xA0C8U, .class_code = 0x04U, .subclass = 0x03U },
    { .vendor_id = 0x1234U, .device_id = 0x9999U, .class_code = 0x03U, .subclass = 0x00U }
};

int pci_get_device_count(void)
{
    return (int)(sizeof(inventory) / sizeof(inventory[0]));
}

const struct pci_device *pci_get_device(int index)
{
    if (index < 0 || index >= pci_get_device_count())
        return 0;
    return &inventory[index];
}

int main(void)
{
    const struct pci_device intel = {
        .vendor_id = 0x8086U, .device_id = 0x56A0U,
        .class_code = 0x03U, .subclass = 0x00U, .revision = 2U
    };
    const struct pci_device amd = {
        .vendor_id = 0x1002U, .device_id = 0x744CU,
        .class_code = 0x03U, .subclass = 0x00U, .revision = 1U
    };
    const struct pci_device intel_audio = {
        .vendor_id = 0x8086U, .device_id = 0xA0C8U,
        .class_code = 0x04U, .subclass = 0x03U
    };
    const struct pci_device unknown_intel = {
        .vendor_id = 0x8086U, .device_id = 0xFFFFU,
        .class_code = 0x03U, .subclass = 0x00U
    };
    const struct pci_device unknown_vendor = {
        .vendor_id = 0x1234U, .device_id = 0x9999U,
        .class_code = 0x03U, .subclass = 0x00U
    };
    gpu_pci_match_t match = {0};
    gpu_device_t device;
    intel_gpu_descriptor_t intel_desc;
    amd_gpu_descriptor_t amd_desc;

    assert(gpu_classify_pci_function(&intel, &match));
    assert(match.vendor == GPU_VENDOR_INTEL);
    assert(match.architecture == GPU_ARCH_INTEL_XE_HPG);
    assert(match.is_known_device_id == 1U);
    assert(intel_gpu_match(&intel));
    assert(intel_gpu_describe(&intel, &intel_desc));
    assert(intel_desc.firmware_required == 1U);
    assert(intel_desc.acceleration_ready == 0U);
    assert(gpu_driver_matches(intel_gpu_driver(), &intel));
    assert(!gpu_driver_matches(intel_gpu_driver(), &amd));

    assert(gpu_classify_pci_function(&amd, &match));
    assert(match.vendor == GPU_VENDOR_AMD);
    assert(match.architecture == GPU_ARCH_AMD_RDNA3);
    assert(amd_gpu_match(&amd));
    assert(amd_gpu_describe(&amd, &amd_desc));
    assert(amd_desc.firmware_required == 1U);
    assert(amd_desc.acceleration_ready == 0U);
    assert(gpu_driver_matches(amd_gpu_driver(), &amd));
    assert(!gpu_driver_matches(amd_gpu_driver(), &intel));

    assert(gpu_classify_pci_function(&unknown_intel, &match));
    assert(match.vendor == GPU_VENDOR_INTEL);
    assert(match.architecture == GPU_ARCH_UNKNOWN);
    assert(match.is_known_device_id == 0U);
    assert(!gpu_classify_pci_function(&intel_audio, &match));
    assert(!gpu_classify_pci_function(&unknown_vendor, &match));
    assert(!gpu_classify_pci_function(0, &match));
    {
        gpu_pci_match_t forged = {0};
        assert(gpu_classify_pci_function(&intel, &forged));
        forged.vendor = GPU_VENDOR_AMD;
        assert(!gpu_device_construct(&device, &intel, &forged));
        assert(gpu_classify_pci_function(&intel, &forged));
        forged.architecture = GPU_ARCH_AMD_RDNA3;
        assert(!gpu_device_construct(&device, &intel, &forged));
    }
    assert(!gpu_classify_pci_function(&intel, 0));

    assert(gpu_device_construct(&device, &amd, &match) == 0);
    assert(gpu_classify_pci_function(&amd, &match));
    assert(gpu_device_construct(&device, &amd, &match));
    assert(device.state == GPU_DEVICE_DISCOVERED);
    assert(device.verified_capabilities == 0U);
    assert(!gpu_device_publish_capabilities(&device, GPU_CAP_3D_ACCELERATION));
    assert(!gpu_device_transition(&device, GPU_DEVICE_RUNNING));
    assert(gpu_device_transition(&device, GPU_DEVICE_PROBING));
    assert(gpu_device_transition(&device, GPU_DEVICE_INITIALIZED));
    assert(gpu_device_transition(&device, GPU_DEVICE_RUNNING));
    assert(gpu_device_publish_capabilities(&device, GPU_CAP_DISPLAY_SCANOUT));
    assert(device.verified_capabilities == GPU_CAP_DISPLAY_SCANOUT);
    assert(!gpu_device_publish_capabilities(&device, 1ULL << 40));
    assert(gpu_device_transition(&device, GPU_DEVICE_QUIESCING));
    assert(device.verified_capabilities == 0U);
    assert(gpu_device_transition(&device, GPU_DEVICE_INITIALIZED));
    assert(!gpu_device_transition(&device, GPU_DEVICE_DISCOVERED));


    {
        gpu_device_t discovered[3];
        size_t discovered_count = 99U;
        assert(!gpu_discover_pci(discovered, 2U, &discovered_count));
        assert(discovered_count == 0U);
        assert(gpu_discover_pci(discovered, 3U, &discovered_count));
        assert(discovered_count == 3U);
        assert(discovered[0].vendor == GPU_VENDOR_INTEL);
        assert(discovered[1].vendor == GPU_VENDOR_AMD);
        assert(discovered[2].vendor == GPU_VENDOR_NVIDIA);
        assert(discovered[2].architecture == GPU_ARCH_NVIDIA_AMPERE);
        assert(discovered[0].state == GPU_DEVICE_DISCOVERED);
        assert(discovered[0].verified_capabilities == 0U);
    }
    assert(strcmp(gpu_vendor_name(GPU_VENDOR_AMD), "AMD") == 0);
    assert(strcmp(gpu_architecture_name(GPU_ARCH_AMD_RDNA3), "AMD RDNA 3") == 0);
    assert(strcmp(gpu_architecture_name(GPU_ARCH_NVIDIA_AMPERE), "NVIDIA Ampere") == 0);
    return 0;
}
