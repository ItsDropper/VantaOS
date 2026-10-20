/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_RESOURCE_H
#define VANTA_NVX_RESOURCE_H

#include <stddef.h>
#include <stdint.h>
#include "pci.h"

typedef enum nvx_resource_status {
    NVX_RESOURCE_OK = 0,
    NVX_RESOURCE_INVALID_ARGUMENT,
    NVX_RESOURCE_ABSENT,
    NVX_RESOURCE_IO_SPACE_UNSUPPORTED,
    NVX_RESOURCE_INVALID_WIDTH,
    NVX_RESOURCE_INVALID_BASE,
    NVX_RESOURCE_SIZE_UNKNOWN,
    NVX_RESOURCE_INVALID_SIZE,
    NVX_RESOURCE_RANGE_OVERFLOW,
    NVX_RESOURCE_MAPPING_FAILED
} nvx_resource_status_t;

typedef struct nvx_bar_resource {
    uint64_t base;
    uint64_t size;
    uint8_t is_io;
    uint8_t is_64bit;
    uint8_t size_known;
} nvx_bar_resource_t;

/* Reads BAR metadata only; never writes configuration space or probes size. */
nvx_resource_status_t nvx_bar_resource_read(
    const struct pci_device *device, uint8_t index,
    nvx_bar_resource_t *out_resource);

/* Validates externally established size metadata before any MMIO mapping. */
nvx_resource_status_t nvx_bar_resource_validate(
    const nvx_bar_resource_t *resource);

/* Maps only validated, size-known memory BARs; caller owns the returned mapping. */
nvx_resource_status_t nvx_bar_mmio_map(
    const nvx_bar_resource_t *resource, void **out_mapping);
void nvx_bar_mmio_unmap(const nvx_bar_resource_t *resource, void *mapping);

#endif
