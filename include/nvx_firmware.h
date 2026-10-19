/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_FIRMWARE_H
#define VANTA_NVX_FIRMWARE_H

#include <stddef.h>
#include <stdint.h>
#include "nvx_device.h"

#define NVX_FIRMWARE_MAX_BYTES (64U * 1024U * 1024U)

typedef struct nvx_firmware_blob {
    const uint8_t *data;
    size_t size;
    void *release_context;
} nvx_firmware_blob_t;

/*
 * The platform provider must implement verify() using its trusted firmware
 * authenticity policy (for example, signature verification against a trusted
 * key). NVX rejects firmware when verification is unavailable or fails.
 */
typedef struct nvx_firmware_provider {
    int (*request)(const char *name, nvx_firmware_blob_t *out_blob, void *context);
    int (*verify)(const char *name, const nvx_firmware_blob_t *blob, void *context);
    void (*release)(nvx_firmware_blob_t *blob, void *context);
    void *context;
} nvx_firmware_provider_t;

typedef enum nvx_firmware_status {
    NVX_FIRMWARE_OK = 0,
    NVX_FIRMWARE_INVALID_ARGUMENT,
    NVX_FIRMWARE_UNSUPPORTED_FAMILY,
    NVX_FIRMWARE_PROVIDER_INCOMPLETE,
    NVX_FIRMWARE_NOT_FOUND,
    NVX_FIRMWARE_INVALID_BLOB,
    NVX_FIRMWARE_TOO_LARGE,
    NVX_FIRMWARE_VERIFICATION_FAILED
} nvx_firmware_status_t;

typedef struct nvx_firmware_image {
    nvx_firmware_blob_t blob;
    nvx_firmware_provider_t provider;
    nvx_generation_family_t family;
    uint8_t acquired;
} nvx_firmware_image_t;

/* Currently only Ampere GSP firmware is selected by the implemented profile table. */
nvx_firmware_status_t nvx_firmware_acquire(
    const nvx_firmware_provider_t *provider,
    nvx_generation_family_t family,
    nvx_firmware_image_t *out_image);
void nvx_firmware_release(nvx_firmware_image_t *image);

#endif
