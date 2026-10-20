/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_IRQ_ROUTE_H
#define VANTA_NVX_IRQ_ROUTE_H

#include <stdint.h>
#include "device_irq.h"
#include "nvx_irq_caps.h"

typedef enum nvx_irq_route_mode {
    NVX_IRQ_ROUTE_NONE = 0,
    NVX_IRQ_ROUTE_MSI,
    NVX_IRQ_ROUTE_MSIX
} nvx_irq_route_mode_t;

typedef enum nvx_irq_route_status {
    NVX_IRQ_ROUTE_OK = 0,
    NVX_IRQ_ROUTE_INVALID_ARGUMENT,
    NVX_IRQ_ROUTE_CAPABILITY_ERROR,
    NVX_IRQ_ROUTE_UNSUPPORTED,
    NVX_IRQ_ROUTE_VECTOR_UNAVAILABLE,
    NVX_IRQ_ROUTE_HANDLER_ERROR,
    NVX_IRQ_ROUTE_NOT_PREPARED,
    NVX_IRQ_ROUTE_DEVICE_ALREADY_ENABLED,
    NVX_IRQ_ROUTE_HARDWARE_CONFIG_FAILED,
    NVX_IRQ_ROUTE_MAPPING_FAILED
} nvx_irq_route_status_t;

/* Zero-initialize the output route before first use. */
/*
 * Reserves one OS vector and registers a callback after validating the device's
 * MSI/MSI-X capability metadata. This does not program PCI MSI registers or the
 * MSI-X table and does not enable delivery.
 */
typedef struct nvx_irq_route {
    const struct pci_device *device;
    nvx_irq_capabilities_t capabilities;
    device_irq_handler_t handler;
    void *context;
    uint8_t vector;
    uint8_t mode;
    nvx_bar_resource_t bars[6];
    void *table_mapping;
    uint64_t table_mapping_size;
    uint8_t prepared;
    uint8_t enabled;
} nvx_irq_route_t;

nvx_irq_route_status_t nvx_irq_route_prepare(
    const struct pci_device *device,
    const nvx_bar_resource_t bars[6],
    device_irq_handler_t handler,
    void *context,
    nvx_irq_route_t *out_route);

/* Disable masks/disables MSI/MSI-X before callback drain and route release. */
nvx_irq_route_status_t nvx_irq_route_enable(nvx_irq_route_t *route);
nvx_irq_route_status_t nvx_irq_route_disable(nvx_irq_route_t *route);
nvx_irq_route_status_t nvx_irq_route_release(nvx_irq_route_t *route);

#endif
