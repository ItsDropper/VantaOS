/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_irq_route.h"
#include "apic.h"
#include "mmio.h"

#include <stddef.h>

static void nvx_irq_route_clear(nvx_irq_route_t *route)
{
    route->device = 0;
    route->capabilities = (nvx_irq_capabilities_t){0};
    route->handler = 0;
    route->context = 0;
    route->vector = 0U;
    route->mode = NVX_IRQ_ROUTE_NONE;
    route->table_mapping = 0;
    route->table_mapping_size = 0U;
    route->prepared = 0U;
    route->enabled = 0U;
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
    for (size_t i = 0; i < 6U; ++i)
        out_route->bars[i] = bars[i];
    out_route->handler = handler;
    out_route->context = context;
    out_route->vector = vector;
    out_route->mode = (uint8_t)mode;
    out_route->prepared = 1U;
    return NVX_IRQ_ROUTE_OK;
}


static uint32_t nvx_irq_msi_address(uint32_t apic_id)
{
    return 0xFEE00000U | ((apic_id & 0xFFU) << 12U);
}

static nvx_irq_route_status_t nvx_irq_enable_msix(nvx_irq_route_t *route)
{
    const nvx_msix_capability_t *msix = &route->capabilities.msix;
    uint8_t control_offset = (uint8_t)(msix->capability_offset + 2U);
    uint16_t control = pci_read_config16(route->device, control_offset);
    uint16_t masked_control;
    nvx_bar_resource_t *bar;
    uint64_t physical;
    volatile uint32_t *table;
    uint32_t address;

    if ((control & 0x8000U) != 0U)
        return NVX_IRQ_ROUTE_DEVICE_ALREADY_ENABLED;
    if (msix->table_bar_index >= 6U || msix->vector_count == 0U)
        return NVX_IRQ_ROUTE_CAPABILITY_ERROR;
    bar = &route->bars[msix->table_bar_index];
    if (!bar->size_known || bar->is_io || bar->size == 0U ||
        msix->table_offset > bar->size ||
        msix->table_bytes > bar->size - msix->table_offset ||
        bar->base > UINT64_MAX - msix->table_offset)
        return NVX_IRQ_ROUTE_CAPABILITY_ERROR;
    physical = bar->base + msix->table_offset;
    if (physical > UINT64_MAX - (msix->table_bytes - 1U))
        return NVX_IRQ_ROUTE_CAPABILITY_ERROR;

    /* Do not take over an already-enabled MSI-X device. */
    masked_control = (uint16_t)((control | 0x4000U) & ~0x8000U);
    if (!pci_write_config16(route->device, control_offset, masked_control))
        return NVX_IRQ_ROUTE_HARDWARE_CONFIG_FAILED;

    route->table_mapping = mmio_map_region(physical, (size_t)msix->table_bytes);
    if (route->table_mapping == 0) {
        (void)pci_write_config16(route->device, control_offset, control);
        return NVX_IRQ_ROUTE_MAPPING_FAILED;
    }
    route->table_mapping_size = msix->table_bytes;
    table = (volatile uint32_t *)route->table_mapping;
    address = nvx_irq_msi_address(apic_get_bsp_id());

    /* Mask the chosen entry before changing its message fields. */
    table[3] = 1U;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    table[0] = address;
    table[1] = 0U;
    table[2] = route->vector;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (table[0] != address || table[1] != 0U ||
        table[2] != route->vector) {
        table[3] = 1U;
        (void)pci_write_config16(route->device, control_offset, masked_control);
        (void)mmio_unmap_region(route->table_mapping,
                                (size_t)route->table_mapping_size);
        route->table_mapping = 0;
        route->table_mapping_size = 0U;
        return NVX_IRQ_ROUTE_HARDWARE_CONFIG_FAILED;
    }

    table[3] = 0U;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    route->enabled = 1U; /* Keep mapping alive if final config readback fails. */
    if (!pci_write_config16(route->device, control_offset,
                            (uint16_t)((control | 0x8000U) & ~0x4000U)))
        return NVX_IRQ_ROUTE_HARDWARE_CONFIG_FAILED;
    return NVX_IRQ_ROUTE_OK;
}

static nvx_irq_route_status_t nvx_irq_enable_msi(nvx_irq_route_t *route)
{
    const nvx_msi_capability_t *msi = &route->capabilities.msi;
    uint8_t control_offset = (uint8_t)(msi->capability_offset + 2U);
    uint16_t control = pci_read_config16(route->device, control_offset);
    uint8_t data_offset;
    uint32_t apic_id = apic_get_bsp_id();
    uint32_t address;

    if ((control & 1U) != 0U)
        return NVX_IRQ_ROUTE_DEVICE_ALREADY_ENABLED;
    if (apic_id > 0xFFU || msi->capability_offset > 0xF0U)
        return NVX_IRQ_ROUTE_CAPABILITY_ERROR;
    address = nvx_irq_msi_address(apic_id);

    if (!pci_write_config32(route->device,
                            (uint8_t)(msi->capability_offset + 4U),
                            address))
        return NVX_IRQ_ROUTE_HARDWARE_CONFIG_FAILED;
    if (msi->supports_64bit_address) {
        if (!pci_write_config32(route->device,
                                (uint8_t)(msi->capability_offset + 8U), 0U))
            return NVX_IRQ_ROUTE_HARDWARE_CONFIG_FAILED;
        data_offset = (uint8_t)(msi->capability_offset + 12U);
    } else {
        data_offset = (uint8_t)(msi->capability_offset + 8U);
    }
    if (!pci_write_config16(route->device, data_offset, route->vector))
        return NVX_IRQ_ROUTE_HARDWARE_CONFIG_FAILED;

    /* Request exactly one message and enable only after address/data are valid. */
    control = (uint16_t)((control & ~0x0070U) | 1U);
    if (!pci_write_config16(route->device, control_offset, control)) {
        uint16_t actual = pci_read_config16(route->device, control_offset);
        if ((actual & 1U) != 0U)
            route->enabled = 1U;
        return NVX_IRQ_ROUTE_HARDWARE_CONFIG_FAILED;
    }
    route->enabled = 1U;
    return NVX_IRQ_ROUTE_OK;
}

nvx_irq_route_status_t nvx_irq_route_enable(nvx_irq_route_t *route)
{
    if (route == 0)
        return NVX_IRQ_ROUTE_INVALID_ARGUMENT;
    if (!route->prepared)
        return NVX_IRQ_ROUTE_NOT_PREPARED;
    if (route->enabled)
        return NVX_IRQ_ROUTE_INVALID_ARGUMENT;
    if (!apic_is_enabled())
        return NVX_IRQ_ROUTE_VECTOR_UNAVAILABLE;
    if (route->mode == NVX_IRQ_ROUTE_MSIX)
        return nvx_irq_enable_msix(route);
    if (route->mode == NVX_IRQ_ROUTE_MSI)
        return nvx_irq_enable_msi(route);
    return NVX_IRQ_ROUTE_UNSUPPORTED;
}

nvx_irq_route_status_t nvx_irq_route_disable(nvx_irq_route_t *route)
{
    if (route == 0)
        return NVX_IRQ_ROUTE_INVALID_ARGUMENT;
    if (!route->prepared)
        return NVX_IRQ_ROUTE_NOT_PREPARED;
    if (!route->enabled)
        return NVX_IRQ_ROUTE_OK;

    if (route->mode == NVX_IRQ_ROUTE_MSIX) {
        uint8_t offset = (uint8_t)(route->capabilities.msix.capability_offset + 2U);
        uint16_t control = pci_read_config16(route->device, offset);
        uint16_t masked = (uint16_t)(control | 0x4000U);
        if (!pci_write_config16(route->device, offset, masked))
            return NVX_IRQ_ROUTE_HARDWARE_CONFIG_FAILED;
        if (!pci_write_config16(route->device, offset,
                                (uint16_t)(masked & ~0x8000U)))
            return NVX_IRQ_ROUTE_HARDWARE_CONFIG_FAILED;
        if (route->table_mapping != 0) {
            volatile uint32_t *table = (volatile uint32_t *)route->table_mapping;
            table[3] = 1U;
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            if (!mmio_unmap_region(route->table_mapping,
                                   (size_t)route->table_mapping_size))
                return NVX_IRQ_ROUTE_MAPPING_FAILED;
            route->table_mapping = 0;
            route->table_mapping_size = 0U;
        }
        route->enabled = 0U;
        return NVX_IRQ_ROUTE_OK;
    }
    if (route->mode == NVX_IRQ_ROUTE_MSI) {
        uint8_t offset = (uint8_t)(route->capabilities.msi.capability_offset + 2U);
        uint16_t control = pci_read_config16(route->device, offset);
        if (!pci_write_config16(route->device, offset,
                                (uint16_t)(control & ~1U)))
            return NVX_IRQ_ROUTE_HARDWARE_CONFIG_FAILED;
        route->enabled = 0U;
        return NVX_IRQ_ROUTE_OK;
    }
    return NVX_IRQ_ROUTE_UNSUPPORTED;
}

nvx_irq_route_status_t nvx_irq_route_release(nvx_irq_route_t *route)
{
    if (route == 0)
        return NVX_IRQ_ROUTE_INVALID_ARGUMENT;
    if (!route->prepared)
        return NVX_IRQ_ROUTE_NOT_PREPARED;
    if (route->enabled) {
        nvx_irq_route_status_t disabled = nvx_irq_route_disable(route);
        if (disabled != NVX_IRQ_ROUTE_OK)
            return disabled;
    }
    if (!device_irq_vector_release(route->vector))
        return NVX_IRQ_ROUTE_HANDLER_ERROR;
    nvx_irq_route_clear(route);
    return NVX_IRQ_ROUTE_OK;
}
