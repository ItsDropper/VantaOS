/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_irq_caps.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define CONFIG_ADDRESS_PORT 0xCF8U
#define CONFIG_DATA_PORT 0xCFCU

static uint32_t config_space[64];
static uint32_t selected_address;

void pci_test_outl(uint16_t port, uint32_t value)
{
    if (port == CONFIG_ADDRESS_PORT) {
        selected_address = value;
        return;
    }
    assert(port == CONFIG_DATA_PORT);
    config_space[(selected_address & 0xFCU) / 4U] = value;
}

uint32_t pci_test_inl(uint16_t port)
{
    assert(port == CONFIG_DATA_PORT);
    return config_space[(selected_address & 0xFCU) / 4U];
}

static void reset_config(void)
{
    memset(config_space, 0, sizeof(config_space));
    selected_address = 0U;
    config_space[1] = 0x00100000U;
}

static void set_byte(uint8_t offset, uint8_t value)
{
    unsigned index = offset / 4U;
    unsigned shift = (unsigned)(offset & 3U) * 8U;
    config_space[index] = (config_space[index] & ~(0xFFU << shift)) |
                          ((uint32_t)value << shift);
}

static void set_word(uint8_t offset, uint16_t value)
{
    set_byte(offset, (uint8_t)value);
    set_byte((uint8_t)(offset + 1U), (uint8_t)(value >> 8U));
}

static void set_dword(uint8_t offset, uint32_t value)
{
    config_space[offset / 4U] = value;
}

int main(void)
{
    const struct pci_device device = {
        .bus = 0U, .slot = 1U, .function = 0U,
        .vendor_id = 0x10DEU, .device_id = 0x2503U
    };
    nvx_bar_resource_t bars[6] = {{0}};
    nvx_irq_capabilities_t caps;

    reset_config();
    set_byte(0x34U, 0x50U);
    set_byte(0x50U, 0x05U); /* MSI */
    set_byte(0x51U, 0x60U);
    set_word(0x52U, (uint16_t)(0x0080U | 0x0100U | (3U << 1U)));
    set_byte(0x60U, 0x11U); /* MSI-X */
    set_byte(0x61U, 0x00U);
    set_word(0x62U, (uint16_t)(0x8000U | 0x4000U | 63U));
    set_dword(0x64U, 0x00001000U); /* table: BAR0 + 0x1000 */
    set_dword(0x68U, 0x00002000U); /* PBA: BAR0 + 0x2000 */

    bars[0].base = 0x80000000ULL;
    bars[0].size = 0x10000U;
    bars[0].size_known = 1U;
    assert(nvx_irq_read_capabilities(&device, bars, &caps) == NVX_IRQ_CAPS_OK);
    assert(caps.msi.present && !caps.msi.enabled);
    assert(caps.msi.supports_64bit_address && caps.msi.per_vector_masking);
    assert(caps.msi.max_messages_log2 == 3U);
    assert(caps.msix.present && caps.msix.enabled && caps.msix.function_masked);
    assert(caps.msix.vector_count == 64U);
    assert(caps.msix.table_bytes == 1024U && caps.msix.pba_bytes == 8U);
    assert(caps.msix.table_offset == 0x1000U && caps.msix.pba_offset == 0x2000U);

    /* Table/PBA outside the trusted BAR extent must be rejected. */
    set_dword(0x64U, 0x0000FFF8U);
    assert(nvx_irq_read_capabilities(&device, bars, &caps) ==
           NVX_IRQ_CAPS_RANGE_INVALID);

    /* An I/O BAR cannot contain an MSI-X table. */
    set_dword(0x64U, 0x00001000U);
    bars[0].is_io = 1U;
    assert(nvx_irq_read_capabilities(&device, bars, &caps) ==
           NVX_IRQ_CAPS_RANGE_INVALID);
    bars[0].is_io = 0U;

    /* Reserved BIR values are rejected. */
    set_dword(0x64U, 0x00001006U);
    assert(nvx_irq_read_capabilities(&device, bars, &caps) ==
           NVX_IRQ_CAPS_INVALID_BAR);

    /* Overlapping table and PBA regions are rejected. */
    set_dword(0x64U, 0x00002000U);
    set_dword(0x68U, 0x00002000U);
    assert(nvx_irq_read_capabilities(&device, bars, &caps) ==
           NVX_IRQ_CAPS_RANGE_INVALID);

    /* MSI multiple-message enable may not exceed the capability. */
    reset_config();
    set_byte(0x34U, 0x50U);
    set_byte(0x50U, 0x05U);
    set_word(0x52U, (uint16_t)(1U << 4U));
    memset(&caps, 0xA5, sizeof(caps));
    assert(nvx_irq_read_capabilities(&device, bars, &caps) ==
           NVX_IRQ_CAPS_MALFORMED_MSI);
    assert(!caps.msi.present);

    assert(nvx_irq_read_capabilities(0, bars, &caps) ==
           NVX_IRQ_CAPS_INVALID_ARGUMENT);
    assert(nvx_irq_read_capabilities(&device, bars, 0) ==
           NVX_IRQ_CAPS_INVALID_ARGUMENT);
    return 0;
}
