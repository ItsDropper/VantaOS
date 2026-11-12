/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_platform.h"

#include <assert.h>
#include <string.h>

static int provider_request(const char *name, nvx_firmware_blob_t *blob,
                            void *context)
{
    (void)name; (void)blob; (void)context;
    return 0;
}
static int provider_verify(const char *name, const nvx_firmware_blob_t *blob,
                           void *context)
{
    (void)name; (void)blob; (void)context;
    return 0;
}
static void provider_release(nvx_firmware_blob_t *blob, void *context)
{
    (void)blob; (void)context;
}

int nvx_match_pci_device(const struct pci_device *device)
{
    return device && device->vendor_id == NVX_PCI_VENDOR_NVIDIA &&
           device->class_code == 0x03U;
}
int nvx_identify_pci_device(uint16_t device_id,
                            nvx_generation_family_t *family,
                            nvx_device_profile_t *profile)
{
    if (!family || !profile)
        return 0;
    if (device_id == 0x2503U) {
        *family = NVX_FAMILY_AMPERE;
        *profile = NVX_PROFILE_GEFORCE_RTX_3060;
        return 1;
    }
    *family = NVX_FAMILY_UNKNOWN;
    *profile = NVX_PROFILE_UNKNOWN;
    return 0;
}

static void delay_ms(uint32_t ms, void *context)
{
    (void)ms; (void)context;
}

static nvx_gsp_platform_config_t valid_config(struct pci_device *device,
                                               nvx_bar_resource_t *bar,
                                               nvx_firmware_provider_t *provider,
                                               nvx_gsp_system_info_t *facts)
{
    nvx_gsp_platform_config_t config = {0};
    config.pci_device = device;
    config.trusted_bar0 = bar;
    config.firmware_provider = provider;
    config.firmware_chip = "ga106";
    config.firmware_version = "535.129.03";
    config.expected_extended_chip_id = 0x177U;
    config.trusted_gpu_facts = facts;
    config.command_queue_bytes = 64U * 1024U;
    config.status_queue_bytes = 64U * 1024U;
    config.timeout_ms = 5000U;
    config.poll_interval_ms = 10U;
    config.delay_ms = delay_ms;
    return config;
}

int main(void)
{
    struct pci_device device = {
        .vendor_id = NVX_PCI_VENDOR_NVIDIA,
        .device_id = 0x2503U,
        .class_code = 0x03U
    };
    nvx_bar_resource_t bar = {
        .base = UINT64_C(0x80000000),
        .size = UINT64_C(0x01000000),
        .is_io = 0U,
        .is_64bit = 1U,
        .size_known = 1U
    };
    nvx_firmware_provider_t provider = {
        .request = provider_request,
        .verify = provider_verify,
        .release = provider_release
    };
    nvx_gsp_system_info_t facts = {0};
    nvx_gsp_platform_config_t config =
        valid_config(&device, &bar, &provider, &facts);

    assert(nvx_gsp_platform_validate_config(&config) == NVX_GSP_PLATFORM_OK);

    config.pci_device = 0;
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_INVALID_ARGUMENT);
    config = valid_config(&device, &bar, &provider, &facts);

    device.device_id = 0x1D10U;
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_UNSUPPORTED_DEVICE);
    device.device_id = 0x2503U;

    config.firmware_chip = "ga104";
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_UNSUPPORTED_DEVICE);
    config = valid_config(&device, &bar, &provider, &facts);

    bar.size_known = 0U;
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_BAR_INVALID);
    bar.size_known = 1U;
    bar.size = 0x1000U;
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_BAR_INVALID);
    bar.size = UINT64_C(0x01000000);

    provider.verify = 0;
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_INVALID_ARGUMENT);
    provider.verify = provider_verify;

    config.firmware_version = "";
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_INVALID_ARGUMENT);
    config = valid_config(&device, &bar, &provider, &facts);

    facts.present = UINT32_C(1) << 31U;
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_INVALID_ARGUMENT);
    facts.present = NVX_GSP_SI_HOST_PAGE_SIZE;
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_INVALID_ARGUMENT);
    facts.present = 0U;

    config.timeout_ms = 0U;
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_INVALID_ARGUMENT);
    config = valid_config(&device, &bar, &provider, &facts);
    config.poll_interval_ms = config.timeout_ms + 1U;
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_INVALID_ARGUMENT);

    config = valid_config(&device, &bar, &provider, &facts);
    config.command_queue_bytes = NVX_GSP_PAGE_SIZE;
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_QUEUE_FAILED);
    config = valid_config(&device, &bar, &provider, &facts);
    config.command_queue_bytes += 1U;
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_QUEUE_FAILED);
    config = valid_config(&device, &bar, &provider, &facts);
    config.command_queue_bytes = (uint64_t)NVX_DMA_MAX_BYTES;
    config.status_queue_bytes = (uint64_t)NVX_DMA_MAX_BYTES;
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_QUEUE_FAILED);

    config = valid_config(&device, &bar, &provider, &facts);
    config.delay_ms = 0;
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_INVALID_ARGUMENT);

    /* Validation is a pure preflight: it must not request firmware or DMA. */
    assert(nvx_gsp_platform_validate_config(&config) ==
           NVX_GSP_PLATFORM_INVALID_ARGUMENT);
    return 0;
}
