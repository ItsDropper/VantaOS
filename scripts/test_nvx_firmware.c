/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_firmware.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint8_t firmware_bytes[16] = {
    0x47, 0x53, 0x50, 0x00, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C
};
static const char *requested_name;
static unsigned requests;
static unsigned verifies;
static unsigned releases;
static int request_success = 1;
static int verification_success = 1;
static int invalid_data;
static size_t reported_size = sizeof(firmware_bytes);

static int mock_request(const char *name, nvx_firmware_blob_t *out, void *context)
{
    (void)context;
    requested_name = name;
    ++requests;
    out->data = invalid_data ? 0 : firmware_bytes;
    out->size = reported_size;
    out->release_context = firmware_bytes;
    return request_success;
}

static int mock_verify(const char *name, const nvx_firmware_blob_t *blob,
                       void *context)
{
    (void)context;
    ++verifies;
    return strcmp(name, "gsp_ga10x.bin") == 0 &&
           blob->data == firmware_bytes && verification_success;
}

static void mock_release(nvx_firmware_blob_t *blob, void *context)
{
    (void)context;
    ++releases;
    blob->data = 0;
    blob->size = 0;
    blob->release_context = 0;
}

int main(void)
{
    nvx_firmware_provider_t provider = {
        .request = mock_request, .verify = mock_verify,
        .release = mock_release, .context = 0
    };
    nvx_firmware_image_t image = {0};

    assert(nvx_firmware_acquire(&provider, NVX_FAMILY_AMPERE, &image)
           == NVX_FIRMWARE_OK);
    assert(image.acquired == 1U);
    assert(image.family == NVX_FAMILY_AMPERE);
    assert(strcmp(requested_name, "gsp_ga10x.bin") == 0);
    assert(verifies == 1U && releases == 0U);
    nvx_firmware_release(&image);
    assert(image.acquired == 0U && image.blob.data == 0);
    assert(releases == 1U);
    nvx_firmware_release(&image);
    assert(releases == 1U);

    assert(nvx_firmware_acquire(&provider, NVX_FAMILY_ADA, &image)
           == NVX_FIRMWARE_UNSUPPORTED_FAMILY);
    assert(nvx_firmware_acquire(0, NVX_FAMILY_AMPERE, &image)
           == NVX_FIRMWARE_PROVIDER_INCOMPLETE);

    provider.verify = 0;
    assert(nvx_firmware_acquire(&provider, NVX_FAMILY_AMPERE, &image)
           == NVX_FIRMWARE_PROVIDER_INCOMPLETE);
    provider.verify = mock_verify;

    request_success = 0;
    assert(nvx_firmware_acquire(&provider, NVX_FAMILY_AMPERE, &image)
           == NVX_FIRMWARE_NOT_FOUND);
    request_success = 1;

    invalid_data = 1;
    assert(nvx_firmware_acquire(&provider, NVX_FAMILY_AMPERE, &image)
           == NVX_FIRMWARE_INVALID_BLOB);
    invalid_data = 0;

    reported_size = (size_t)NVX_FIRMWARE_MAX_BYTES + 1U;
    assert(nvx_firmware_acquire(&provider, NVX_FAMILY_AMPERE, &image)
           == NVX_FIRMWARE_TOO_LARGE);
    reported_size = sizeof(firmware_bytes);

    verification_success = 0;
    assert(nvx_firmware_acquire(&provider, NVX_FAMILY_AMPERE, &image)
           == NVX_FIRMWARE_VERIFICATION_FAILED);
    assert(image.acquired == 0U);
    assert(releases >= 4U);
    return 0;
}
