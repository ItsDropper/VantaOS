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
typedef struct nvx_firmware_blob {
    const uint8_t *data;
    size_t size;
    void *release_context;
} nvx_firmware_blob_t;
/* Inject the provider so NVX does not depend on VFS paths or filesystem internals. */
typedef struct nvx_firmware_provider {
    int (*request)(const char *name, nvx_firmware_blob_t *out_blob, void *context);
    void (*release)(nvx_firmware_blob_t *blob, void *context);
    void *context;
} nvx_firmware_provider_t;
#endif
