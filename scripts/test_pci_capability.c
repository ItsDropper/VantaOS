/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "pci.h"

#include <assert.h>
#include <stdint.h>

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
    for (unsigned i = 0; i < 64U; ++i)
        config_space[i] = 0U;
    selected_address = 0U;
    config_space[3] = 0U; /* Type-0 header. */
    config_space[1] = 0x00100000U; /* Status.capabilities-list bit. */
}

static void set_byte(uint8_t offset, uint8_t value)
{
    unsigned index = offset / 4U;
    unsigned shift = (unsigned)(offset & 3U) * 8U;
    config_space[index] = (config_space[index] & ~(0xFFU << shift)) |
                          ((uint32_t)value << shift);
}

int main(void)
{
    const struct pci_device device = {
        .bus = 0U, .slot = 2U, .function = 0U,
        .vendor_id = 0x10DEU, .device_id = 0x2503U
    };
    uint8_t offset = 0xAAU;

    reset_config();
    set_byte(0x34U, 0x50U);
    set_byte(0x50U, 0x01U); /* Power management. */
    set_byte(0x51U, 0x60U);
    set_byte(0x60U, 0x05U); /* MSI. */
    set_byte(0x61U, 0x70U);
    set_byte(0x70U, 0x11U); /* MSI-X. */
    set_byte(0x71U, 0x00U);
    assert(pci_find_capability(&device, 0x05U, &offset));
    assert(offset == 0x60U);
    assert(pci_find_capability(&device, 0x11U, &offset));
    assert(offset == 0x70U);
    assert(!pci_find_capability(&device, 0x10U, &offset));
    assert(offset == 0U);

    /* No capability-list status bit means the pointer must not be followed. */
    config_space[1] &= ~0x00100000U;
    offset = 0xAAU;
    assert(!pci_find_capability(&device, 0x05U, &offset));
    assert(offset == 0U);

    reset_config();
    set_byte(0x34U, 0x51U); /* Misaligned pointer. */
    assert(!pci_find_capability(&device, 0x05U, &offset));
    reset_config();
    set_byte(0x34U, 0x40U);
    set_byte(0x40U, 0x01U);
    set_byte(0x41U, 0x40U); /* Self-cycle. */
    assert(!pci_find_capability(&device, 0x05U, &offset));
    reset_config();
    set_byte(0x34U, 0xFCU);
    set_byte(0xFCU, 0x01U);
    set_byte(0xFDU, 0x40U); /* Cycle at final valid dword. */
    assert(!pci_find_capability(&device, 0x05U, &offset));

    reset_config();
    config_space[3] = 0x00020000U; /* CardBus header is not supported here. */
    set_byte(0x34U, 0x50U);
    assert(!pci_find_capability(&device, 0x05U, &offset));

    assert(!pci_find_capability(0, 0x05U, &offset));
    assert(!pci_find_capability(&device, 0U, &offset));
    assert(!pci_find_capability(&device, 0xFFU, &offset));
    assert(!pci_find_capability(&device, 0x05U, 0));
    return 0;
}
