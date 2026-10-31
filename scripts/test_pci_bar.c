/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "pci.h"
#include "ktrace.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define PCI_CONFIG_ADDRESS 0xCF8U
#define PCI_CONFIG_DATA    0xCFCU

static uint32_t config_space[64];
static uint32_t bar_masks[6];
static uint8_t bar_probe[6];
static uint32_t selected_address;
static uint32_t config_writes;

void ktrace_emit(uint32_t type, uint32_t cpu_id, uint64_t arg0, uint64_t arg1)
{
    (void)type;
    (void)cpu_id;
    (void)arg0;
    (void)arg1;
}

void pci_test_outl(uint16_t port, uint32_t value)
{
    if (port == PCI_CONFIG_ADDRESS) {
        selected_address = value;
        return;
    }
    assert(port == PCI_CONFIG_DATA);
    uint32_t offset = selected_address & 0xFCU;
    uint32_t index = offset / 4U;
    if (offset >= 0x10U && offset <= 0x24U && value == UINT32_MAX) {
        bar_probe[(offset - 0x10U) / 4U] = 1U;
        ++config_writes;
        return;
    }
    if (offset >= 0x10U && offset <= 0x24U)
        bar_probe[(offset - 0x10U) / 4U] = 0U;
    config_space[index] = value;
    ++config_writes;
}

uint32_t pci_test_inl(uint16_t port)
{
    assert(port == PCI_CONFIG_DATA);
    uint32_t offset = selected_address & 0xFCU;
    uint32_t index = offset / 4U;
    if (offset >= 0x10U && offset <= 0x24U) {
        uint32_t bar_index = (offset - 0x10U) / 4U;
        if (bar_probe[bar_index])
            return bar_masks[bar_index];
    }
    return config_space[index];
}

int pci_test_ecam_read32(uint16_t segment, uint8_t bus, uint8_t slot,
                         uint8_t function, uint16_t offset, uint32_t* value)
{
    (void)segment;
    (void)bus;
    (void)slot;
    (void)function;
    if (!value || offset < 0x100U || offset > 0xFFCU || (offset & 3U))
        return 0;
    *value = 0U;
    return 1;
}

int pci_test_ecam_write32(uint16_t segment, uint8_t bus, uint8_t slot,
                          uint8_t function, uint16_t offset, uint32_t value)
{
    (void)segment; (void)bus; (void)slot; (void)function;
    (void)offset; (void)value;
    return 0;
}

static void reset_config(void)
{
    for (uint32_t i = 0; i < 64U; ++i)
        config_space[i] = 0U;
    for (uint32_t i = 0; i < 6U; ++i) {
        bar_masks[i] = 0U;
        bar_probe[i] = 0U;
    }
    selected_address = 0U;
    config_writes = 0U;
    config_space[1] = 0x00100000U; /* Status bit; command register is disabled. */
    config_space[3] = 0U;          /* Type-0 header. */
}

int main(void)
{
    const struct pci_device device = {
        .bus = 0U, .slot = 5U, .function = 0U,
        .vendor_id = 0x8086U, .device_id = 0x2922U,
        .class_code = 0x01U, .subclass = 0x06U, .prog_if = 0x01U
    };
    uint64_t size = 99U;
    uint64_t address = 0U;
    int is_io = 1;
    int is_64bit = 1;

    reset_config();
    config_space[9] = 0xFEBF0000U; /* BAR5: 32-bit memory BAR. */
    bar_masks[5] = 0xFFFFE000U;     /* 8 KiB aperture. */
    assert(pci_get_bar_size(&device, 5U, &size));
    assert(size == 0x2000U);
    assert(config_space[9] == 0xFEBF0000U);
    assert(bar_probe[5] == 0U);
    assert(pci_get_bar(&device, 5U, &address, &is_io, &is_64bit));
    assert(address == 0xFEBF0000ULL);
    assert(is_io == 0 && is_64bit == 0);

    /* Sizing must refuse to modify BARs while decode or bus mastering is active. */
    config_space[1] |= 0x0002U;
    size = 99U;
    uint32_t writes_before = config_writes;
    assert(!pci_get_bar_size(&device, 5U, &size));
    assert(size == 0U);
    assert(config_writes == writes_before);
    assert(config_space[9] == 0xFEBF0000U);

    reset_config();
    /* BAR5 is not independently usable when BAR4 consumes it as its high half. */
    config_space[8] = 0x00001004U;
    config_space[9] = 0x00000001U;
    size = 99U;
    assert(!pci_get_bar_size(&device, 5U, &size));
    assert(size == 0U);
    assert(!pci_get_bar(&device, 5U, &address, &is_io, &is_64bit));

    reset_config();
    /* Reject a non-power-of-two size mask and still restore the BAR. */
    config_space[9] = 0xFEBF0000U;
    bar_masks[5] = 0xFFFFD000U;
    size = 99U;
    assert(!pci_get_bar_size(&device, 5U, &size));
    assert(size == 0U);
    assert(config_space[9] == 0xFEBF0000U);
    assert(bar_probe[5] == 0U);

    puts("VANTAOS_PCI_BAR_OK");
    return 0;
}
