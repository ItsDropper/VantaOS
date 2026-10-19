/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_irq_route.h"

#include <stddef.h>

static void nvx_irq_route_clear(nvx_irq_route_t *route)
{
    route->device = 0;
    route->capabilities = (nvx_irq_capabilities_t){0};
    route->handler = 0;
    route->context = 0;
    route->vector = 0U;
    route->mode = NVX_IRQ_ROUTE_NONE;
    route->prepared = 0U;
}

nvx_irq_route_status_t nvx_irq_route_prepare(
    const struct pci_device *device,
    const nvx_bar_resource_t bars[6],
    device_irq_handler_t handler,
    void *context,
    nvx_irq_route_t *out_route)
{
    nvx_irq_capabilities_t caps = {0};
    nvx_irq_caps_status_t caps_status;
    uint8_t vector = 0U;
    nvx_irq_route_mode_t mode;

    if (out_route == 0)
        return NVX_IRQ_ROUTE_INVALID_ARGUMENT;
    if (out_route->prepared)
        return NVX_IRQ_ROUTE_INVALID_ARGUMENT;
    nvx_irq_route_clear(out_route);
    if (device == 0 || bars == 0 || handler == 0)
        return NVX_IRQ_ROUTE_INVALID_ARGUMENT;

    caps_status = nvx_irq_read_capabilities(device, bars, &caps);
    if (caps_status != NVX_IRQ_CAPS_OK)
        return NVX_IRQ_ROUTE_CAPABILITY_ERROR;
    if (caps.msix.present)
        mode = NVX_IRQ_ROUTE_MSIX;
    else if (caps.msi.present)
        mode = NVX_IRQ_ROUTE_MSI;
    else
        return NVX_IRQ_ROUTE_UNSUPPORTED;

    if (!device_irq_vector_claim(&vector))
        return NVX_IRQ_ROUTE_VECTOR_UNAVAILABLE;
    if (!device_irq_vector_register(vector, handler, context)) {
        (void)device_irq_vector_release(vector);
        return NVX_IRQ_ROUTE_HANDLER_ERROR;
    }

    out_route->device = device;
    out_route->capabilities = caps;
    out_route->handler = handler;
    out_route->context = context;
    out_route->vector = vector;
    out_route->mode = (uint8_t)mode;
    out_route->prepared = 1U;
    return NVX_IRQ_ROUTE_OK;
}

nvx_irq_route_status_t nvx_irq_route_release(nvx_irq_route_t *route)
{
    if (route == 0)
        return NVX_IRQ_ROUTE_INVALID_ARGUMENT;
    if (!route->prepared)
        return NVX_IRQ_ROUTE_NOT_PREPARED;
    if (!device_irq_vector_release(route->vector))
        return NVX_IRQ_ROUTE_HANDLER_ERROR;
    nvx_irq_route_clear(route);
    return NVX_IRQ_ROUTE_OK;
}
