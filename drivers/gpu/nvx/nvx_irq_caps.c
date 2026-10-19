/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_irq_caps.h"

#include <stddef.h>
#include <stdint.h>

static int nvx_irq_add_overflow(uint64_t a, uint64_t b, uint64_t *out)
{
    if (out == 0 || a > UINT64_MAX - b)
        return 1;
    *out = a + b;
    return 0;
}

static int nvx_irq_msix_region_valid(const nvx_bar_resource_t *bar,
                                     uint32_t offset, uint64_t bytes)
{
    uint64_t end;
    if (bar == 0 || bar->is_io || !bar->size_known ||
        bar->base == 0U || bar->size == 0U)
        return 0;
    if (nvx_irq_add_overflow(offset, bytes, &end) || end > bar->size)
        return 0;
    if (nvx_irq_add_overflow(bar->base, end, &end))
        return 0;
    return 1;
}

nvx_irq_caps_status_t nvx_irq_read_capabilities(
    const struct pci_device *device,
    const nvx_bar_resource_t bars[6],
    nvx_irq_capabilities_t *out_caps)
{
    uint8_t offset = 0U;
    nvx_irq_capabilities_t result = {0};

    if (out_caps != 0)
        *out_caps = result;
    if (device == 0 || bars == 0 || out_caps == 0)
        return NVX_IRQ_CAPS_INVALID_ARGUMENT;

    if (pci_find_capability(device, 0x05U, &offset)) {
        uint16_t control;
        if (offset > 0xF8U)
            return NVX_IRQ_CAPS_MALFORMED_MSI;
        control = pci_read_config16(device, (uint8_t)(offset + 2U));
        result.msi.present = 1U;
        result.msi.enabled = (uint8_t)(control & 1U);
        result.msi.max_messages_log2 = (uint8_t)((control >> 1U) & 7U);
        result.msi.multiple_message_enable_log2 =
            (uint8_t)((control >> 4U) & 7U);
        result.msi.supports_64bit_address = (uint8_t)((control >> 7U) & 1U);
        result.msi.per_vector_masking = (uint8_t)((control >> 8U) & 1U);
        if (result.msi.multiple_message_enable_log2 >
            result.msi.max_messages_log2)
            return NVX_IRQ_CAPS_MALFORMED_MSI;
        result.msi.capability_offset = offset;
    }

    offset = 0U;
    if (pci_find_capability(device, 0x11U, &offset)) {
        uint16_t control;
        uint32_t table, pba;
        uint64_t table_bytes, pba_bytes, table_end, pba_end;
        uint8_t table_bir, pba_bir;

        if (offset > 0xF4U)
            return NVX_IRQ_CAPS_MALFORMED_MSIX;
        control = pci_read_config16(device, (uint8_t)(offset + 2U));
        table = pci_read_config32(device, (uint8_t)(offset + 4U));
        pba = pci_read_config32(device, (uint8_t)(offset + 8U));
        table_bir = (uint8_t)(table & 7U);
        pba_bir = (uint8_t)(pba & 7U);
        table = table & ~7U;
        pba = pba & ~7U;

        result.msix.present = 1U;
        result.msix.enabled = (uint8_t)((control >> 15U) & 1U);
        result.msix.function_masked = (uint8_t)((control >> 14U) & 1U);
        result.msix.vector_count = (uint16_t)((control & 0x07FFU) + 1U);
        result.msix.table_bar_index = table_bir;
        result.msix.pba_bar_index = pba_bir;
        result.msix.table_offset = table;
        result.msix.pba_offset = pba;
        result.msix.capability_offset = offset;

        table_bytes = (uint64_t)result.msix.vector_count * 16U;
        pba_bytes = (((uint64_t)result.msix.vector_count + 63U) / 64U) * 8U;
        result.msix.table_bytes = table_bytes;
        result.msix.pba_bytes = pba_bytes;

        if (table_bir >= 6U || pba_bir >= 6U)
            return NVX_IRQ_CAPS_INVALID_BAR;
        if (!nvx_irq_msix_region_valid(&bars[table_bir], table, table_bytes) ||
            !nvx_irq_msix_region_valid(&bars[pba_bir], pba, pba_bytes))
            return NVX_IRQ_CAPS_RANGE_INVALID;

        table_end = (uint64_t)table + table_bytes;
        pba_end = (uint64_t)pba + pba_bytes;
        if (table_bir == pba_bir &&
            (uint64_t)table < pba_end && (uint64_t)pba < table_end)
            return NVX_IRQ_CAPS_RANGE_INVALID;
    }

    *out_caps = result;
    return NVX_IRQ_CAPS_OK;
}
