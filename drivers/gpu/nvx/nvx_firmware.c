/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_firmware.h"

static const char *nvx_firmware_name(nvx_generation_family_t family)
{
    switch (family)
    {
        case NVX_FAMILY_AMPERE:
            return "gsp_ga10x.bin";
        default:
            return 0;
    }
}

static void nvx_blob_clear(nvx_firmware_blob_t *blob)
{
    blob->data = 0;
    blob->size = 0;
    blob->release_context = 0;
}

nvx_firmware_status_t nvx_firmware_acquire(
    const nvx_firmware_provider_t *provider,
    nvx_generation_family_t family,
    nvx_firmware_image_t *out_image)
{
    const char *name;
    nvx_firmware_blob_t blob = {0};

    if (out_image == 0 || out_image->acquired)
        return NVX_FIRMWARE_INVALID_ARGUMENT;

    nvx_blob_clear(&out_image->blob);
    out_image->provider = (nvx_firmware_provider_t){0};
    out_image->family = NVX_FAMILY_UNKNOWN;
    out_image->acquired = 0;

    name = nvx_firmware_name(family);
    if (name == 0)
        return NVX_FIRMWARE_UNSUPPORTED_FAMILY;
    if (provider == 0 || provider->request == 0 ||
        provider->verify == 0 || provider->release == 0)
        return NVX_FIRMWARE_PROVIDER_INCOMPLETE;

    if (!provider->request(name, &blob, provider->context)) {
        /* Providers may partially fill a blob before reporting failure. */
        if (blob.data != 0 || blob.release_context != 0)
            provider->release(&blob, provider->context);
        return NVX_FIRMWARE_NOT_FOUND;
    }

    if (blob.data == 0 || blob.size == 0) {
        provider->release(&blob, provider->context);
        return NVX_FIRMWARE_INVALID_BLOB;
    }
    if (blob.size > (size_t)NVX_FIRMWARE_MAX_BYTES) {
        provider->release(&blob, provider->context);
        return NVX_FIRMWARE_TOO_LARGE;
    }
    if (!provider->verify(name, &blob, provider->context)) {
        provider->release(&blob, provider->context);
        return NVX_FIRMWARE_VERIFICATION_FAILED;
    }

    out_image->blob = blob;
    out_image->provider = *provider;
    out_image->family = family;
    out_image->acquired = 1;
    return NVX_FIRMWARE_OK;
}

void nvx_firmware_release(nvx_firmware_image_t *image)
{
    if (image == 0 || !image->acquired)
        return;

    image->provider.release(&image->blob, image->provider.context);
    nvx_blob_clear(&image->blob);
    image->provider = (nvx_firmware_provider_t){0};
    image->family = NVX_FAMILY_UNKNOWN;
    image->acquired = 0;
}
