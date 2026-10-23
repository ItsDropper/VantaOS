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


static int test_probe_fails;
static int test_quiesce_fails;
static int test_initialize_fails;
static int test_probe_calls;
static int test_initialize_calls;
static int test_shutdown_calls;

static int test_driver_match(const struct pci_device *device)
{
    return device != 0 && device->vendor_id == 0x8086U;
}

static int test_driver_probe(gpu_device_t *device)
{
    ++test_probe_calls;
    return device != 0 && device->state == GPU_DEVICE_PROBING && !test_probe_fails;
}

static int test_driver_initialize(gpu_device_t *device)
{
    ++test_initialize_calls;
    return device != 0 && !test_initialize_fails;
}

static int test_driver_quiesce(gpu_device_t *device)
{
    return device != 0 && device->state == GPU_DEVICE_QUIESCING && !test_quiesce_fails;
}

static void test_driver_shutdown(gpu_device_t *device)
{
    ++test_shutdown_calls;
    gpu_device_clear_capabilities(device);
}

static const gpu_driver_ops_t test_ops = {
    .probe = test_driver_probe,
    .initialize = test_driver_initialize,
    .quiesce = test_driver_quiesce,
    .shutdown = test_driver_shutdown
};

static const gpu_driver_descriptor_t test_driver = {
    .name = "test-gpu", .vendor_id = 0x8086U,
    .match = test_driver_match, .ops = &test_ops
};

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

/* The framework test intentionally models GPUs with unassigned BARs. */
uint8_t pci_read_config8(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    return offset == 0x0EU ? 0U : 0U;
}

uint16_t pci_read_config16(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    (void)offset;
    return 0U;
}

uint32_t pci_read_config32(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    (void)offset;
    return 0U;
}

int pci_get_bar(const struct pci_device *device, uint8_t index,
                uint64_t *address, int *is_io, int *is_64bit)
{
    (void)device;
    (void)index;
    if (address != 0) *address = 0U;
    if (is_io != 0) *is_io = 0;
    if (is_64bit != 0) *is_64bit = 0;
    return 0;
}

const struct pci_device *pci_get_device(int index)
{
    if (index < 0 || index >= pci_get_device_count())
        return 0;
    return &inventory[index];
}

/*
 * This framework-only test does not link the hardware runtime. Its scenarios
 * deliberately have no assigned BARs or firmware, so every runtime entrypoint
 * must fail closed if accidentally reached.
 */
amd_polaris_device_status_t amd_polaris_device_runtime_init(
    amd_polaris_device_runtime_t *runtime, const struct pci_device *device,
    size_t ring_bytes, int firmware_ready, int device_quiesced)
{
    (void)runtime; (void)device; (void)ring_bytes;
    (void)firmware_ready; (void)device_quiesced;
    return AMD_POLARIS_DEVICE_NOT_READY;
}

amd_polaris_device_status_t amd_polaris_device_submit_fence(
    amd_polaris_device_runtime_t *runtime, uint32_t *out_fence)
{
    (void)runtime;
    if (out_fence != NULL) *out_fence = 0U;
    return AMD_POLARIS_DEVICE_NOT_READY;
}

amd_polaris_device_status_t amd_polaris_device_wait_fence(
    amd_polaris_device_runtime_t *runtime, uint32_t fence,
    unsigned timeout_iterations)
{
    (void)runtime; (void)fence; (void)timeout_iterations;
    return AMD_POLARIS_DEVICE_NOT_READY;
}

amd_polaris_device_status_t amd_polaris_device_runtime_shutdown(
    amd_polaris_device_runtime_t *runtime, int device_quiesced)
{
    (void)runtime; (void)device_quiesced;
    return AMD_POLARIS_DEVICE_NOT_READY;
}

intel_gen9_device_status_t intel_gen9_device_runtime_init(
    intel_gen9_device_runtime_t *runtime, const struct pci_device *device,
    uint64_t lrca_ggtt_address, uint32_t context_id,
    uint16_t descriptor_flags, int context_ready, int device_quiesced)
{
    (void)runtime; (void)device; (void)lrca_ggtt_address;
    (void)context_id; (void)descriptor_flags;
    (void)context_ready; (void)device_quiesced;
    return INTEL_GEN9_DEVICE_NOT_READY;
}

intel_gen9_device_status_t intel_gen9_device_submit(
    intel_gen9_device_runtime_t *runtime)
{
    (void)runtime;
    return INTEL_GEN9_DEVICE_NOT_READY;
}

intel_gen9_device_status_t intel_gen9_device_wait(
    intel_gen9_device_runtime_t *runtime, unsigned timeout_iterations)
{
    (void)runtime; (void)timeout_iterations;
    return INTEL_GEN9_DEVICE_NOT_READY;
}

intel_gen9_device_status_t intel_gen9_device_runtime_shutdown(
    intel_gen9_device_runtime_t *runtime, int device_quiesced)
{
    (void)runtime; (void)device_quiesced;
    return INTEL_GEN9_DEVICE_NOT_READY;
}

intel_gen9_execlist_status_t intel_gen9_execlist_build_descriptor(
    uint64_t lrca_ggtt_address, uint32_t context_id, uint16_t flags,
    uint64_t *out_descriptor)
{
    if (out_descriptor == NULL || lrca_ggtt_address == 0U ||
        (lrca_ggtt_address & 0xFFFU) != 0U ||
        lrca_ggtt_address > 0x00000000FFFFF000ULL ||
        context_id == 0U || context_id > 0x1FFFFFU ||
        (flags & 0x0001U) == 0U ||
        (((uint64_t)flags) & ~0x0000000000000119ULL) != 0U) {
        if (out_descriptor != NULL) *out_descriptor = 0U;
        return INTEL_GEN9_EXECLIST_INVALID_DESCRIPTOR;
    }
    *out_descriptor = lrca_ggtt_address |
                      ((uint64_t)context_id << 32U) | flags;
    return INTEL_GEN9_EXECLIST_OK;
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
    assert(intel_desc.gen9_profile_status == INTEL_GEN9_UNSUPPORTED_DEVICE);
    assert(intel_desc.firmware_required == 1U);
    assert(intel_desc.acceleration_ready == 0U);
    assert(intel_desc.resources.count == 0U);
    assert(intel_desc.mmio_aperture_enabled == 0U);
    assert(gpu_driver_matches(intel_gpu_driver(), &intel));
    assert(!gpu_driver_matches(intel_gpu_driver(), &amd));
    assert(gpu_classify_pci_function(&intel, &match));
    assert(gpu_device_construct(&device, &intel, &match));
    assert(!gpu_driver_start(&device, intel_gpu_driver()));
    assert(device.state == GPU_DEVICE_FAILED);
    assert(device.verified_capabilities == 0U);

    assert(gpu_classify_pci_function(&amd, &match));
    assert(match.vendor == GPU_VENDOR_AMD);
    assert(match.architecture == GPU_ARCH_AMD_RDNA3);
    assert(amd_gpu_match(&amd));
    assert(amd_gpu_describe(&amd, &amd_desc));
    assert(amd_desc.polaris_profile_status == AMD_POLARIS_UNSUPPORTED_DEVICE);
    assert(amd_desc.firmware_required == 1U);
    assert(amd_desc.acceleration_ready == 0U);
    assert(amd_desc.resources.count == 0U);
    assert(amd_desc.mmio_aperture_enabled == 0U);
    assert(gpu_driver_matches(amd_gpu_driver(), &amd));
    assert(!gpu_driver_matches(amd_gpu_driver(), &intel));
    assert(gpu_device_construct(&device, &amd, &match));
    assert(!gpu_driver_start(&device, amd_gpu_driver()));
    assert(device.state == GPU_DEVICE_FAILED);
    assert(device.verified_capabilities == 0U);

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
    {
        gpu_device_t attached;
        gpu_pci_match_t intel_match;
        assert(gpu_classify_pci_function(&intel, &intel_match));
        assert(gpu_device_construct(&attached, &intel, &intel_match));
        test_probe_fails = 0;
        test_initialize_fails = 0;
        test_probe_calls = test_initialize_calls = test_shutdown_calls = 0;
        assert(gpu_driver_start(&attached, &test_driver));
        assert(attached.state == GPU_DEVICE_RUNNING);
        assert(attached.ops == &test_ops);
        assert(test_probe_calls == 1 && test_initialize_calls == 1);
        assert(gpu_device_publish_capabilities(&attached, GPU_CAP_DISPLAY_SCANOUT));
        assert(gpu_driver_stop(&attached));
        assert(attached.state == GPU_DEVICE_STOPPED);
        assert(attached.ops == 0);
        assert(attached.verified_capabilities == 0U);
        assert(test_shutdown_calls == 1);
        assert(!gpu_driver_stop(&attached));

        assert(gpu_device_construct(&attached, &intel, &intel_match));
        test_initialize_fails = 1;
        assert(!gpu_driver_start(&attached, &test_driver));
        assert(attached.state == GPU_DEVICE_FAILED);
        assert(attached.verified_capabilities == 0U);
        assert(test_shutdown_calls == 2);
        assert(!gpu_driver_start(&attached, &test_driver));
        assert(test_initialize_calls == 2);

        assert(gpu_device_construct(&attached, &intel, &intel_match));
        test_initialize_fails = 0;
        test_quiesce_fails = 0;
        assert(gpu_driver_start(&attached, &test_driver));
        assert(gpu_device_publish_capabilities(&attached, GPU_CAP_DISPLAY_SCANOUT));
        test_quiesce_fails = 1;
        assert(!gpu_driver_stop(&attached));
        assert(attached.state == GPU_DEVICE_FAILED);
        assert(attached.ops == 0);
        assert(attached.verified_capabilities == 0U);
        assert(test_shutdown_calls == 3);

        assert(gpu_device_construct(&attached, &intel, &intel_match));
        test_quiesce_fails = 0;
        test_probe_fails = 1;
        assert(!gpu_driver_start(&attached, &test_driver));
        assert(attached.state == GPU_DEVICE_FAILED);
        assert(test_shutdown_calls == 4);
        assert(test_initialize_calls == 3);
    }
    {
        const struct pci_device polaris = {
            .vendor_id = 0x1002U, .device_id = 0x67DFU,
            .class_code = 0x03U, .subclass = 0x00U
        };
        gpu_device_t amd_device;
        gpu_pci_match_t amd_match;
        amd_gpu_runtime_binding_t amd_binding = {0};
        intel_gpu_runtime_binding_t intel_binding = {0};
        gpu_device_t intel_device;
        gpu_pci_match_t intel_match;
        const struct pci_device gen9 = {
            .vendor_id = 0x8086U, .device_id = 0x1912U,
            .class_code = 0x03U, .subclass = 0x00U
        };

        assert(gpu_classify_pci_function(&polaris, &amd_match));
        assert(gpu_device_construct(&amd_device, &polaris, &amd_match));
        assert(!amd_gpu_configure_runtime(&amd_device, &amd_binding,
            65536U, 100U, 0));
        assert(amd_device.driver_private == NULL);
        assert(amd_gpu_configure_runtime(&amd_device, &amd_binding,
            65536U, 100U, 1));
        assert(amd_device.driver_private == &amd_binding);
        assert(amd_binding.configured && amd_binding.firmware_ready);
        assert(!amd_gpu_configure_runtime(&amd_device, &amd_binding,
            65536U, 100U, 1));

        assert(gpu_classify_pci_function(&gen9, &intel_match));
        assert(gpu_device_construct(&intel_device, &gen9, &intel_match));
        assert(intel_gpu_configure_runtime(&intel_device, &intel_binding,
            0x1000U, 1U, 0x9U, 100U, 1, test_driver_quiesce, NULL));
        assert(intel_device.driver_private == &intel_binding);
        assert(intel_binding.configured && intel_binding.context_ready);
        assert(!intel_gpu_configure_runtime(&intel_device, &intel_binding,
            0x1000U, 1U, 0x9U, 100U, 1, test_driver_quiesce, NULL));
    }
    assert(strcmp(gpu_vendor_name(GPU_VENDOR_AMD), "AMD") == 0);
    assert(strcmp(gpu_architecture_name(GPU_ARCH_AMD_RDNA3), "AMD RDNA 3") == 0);
    assert(strcmp(gpu_architecture_name(GPU_ARCH_NVIDIA_AMPERE), "NVIDIA Ampere") == 0);
    return 0;
}
