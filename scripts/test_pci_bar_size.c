/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "pci.h"
#include "vanta_boot_info.h"

#include <assert.h>
#include <stdint.h>

#define TEST_CONFIG_ADDRESS 0xCF8U
#define TEST_CONFIG_DATA    0xCFCU
#define TEST_BAR0_INDEX     4U

static vanta_boot_info_t test_boot_info;
const vanta_boot_info_t* kernel_get_boot_info(void)
{
    return &test_boot_info;
}

static uint32_t config_space[64];
static uint32_t bar_masks[6];
static uint8_t bar_probe_active[6];
static uint32_t current_config_address;
static unsigned bar_write_count;

void pci_test_outl(uint16_t port, uint32_t value)
{
    if (port == TEST_CONFIG_ADDRESS) {
        current_config_address = value;
        return;
    }
    assert(port == TEST_CONFIG_DATA);

    uint8_t offset = (uint8_t)(current_config_address & 0xFCU);
    unsigned dword = (unsigned)offset / 4U;
    if (offset >= 0x10U && offset <= 0x24U) {
        unsigned bar = (unsigned)(offset - 0x10U) / 4U;
        ++bar_write_count;
        if (value == 0xFFFFFFFFU) {
            bar_probe_active[bar] = 1U;
            return;
        }
        config_space[dword] = value;
        bar_probe_active[bar] = 0U;
        return;
    }
    config_space[dword] = value;
}

uint32_t pci_test_inl(uint16_t port)
{
    assert(port == TEST_CONFIG_DATA);
    uint8_t offset = (uint8_t)(current_config_address & 0xFCU);
    unsigned dword = (unsigned)offset / 4U;
    if (offset >= 0x10U && offset <= 0x24U) {
        unsigned bar = (unsigned)(offset - 0x10U) / 4U;
        if (bar_probe_active[bar])
            return bar_masks[bar];
    }
    return config_space[dword];
}

static void reset_config(void)
{
    for (unsigned i = 0; i < 64U; ++i)
        config_space[i] = 0U;
    for (unsigned i = 0; i < 6U; ++i) {
        bar_masks[i] = 0U;
        bar_probe_active[i] = 0U;
    }
    current_config_address = 0U;
    bar_write_count = 0U;
    /* Type-0 endpoint header; PCI command bits all disabled. */
    config_space[3] = 0U;
    config_space[1] = 0U;
}

int main(void)
{
    const struct pci_device gpu = {
        .bus = 0U, .slot = 0U, .function = 0U,
        .vendor_id = 0x10DEU, .device_id = 0x2503U,
        .class_code = 0x03U
    };
    uint64_t size = UINT64_MAX;

    reset_config();
    config_space[TEST_BAR0_INDEX] = 0x80000000U;
    bar_masks[0] = 0xFF000000U; /* 16 MiB memory BAR */
    assert(pci_get_bar_size(&gpu, 0U, &size));
    assert(size == 0x01000000ULL);
    assert(config_space[TEST_BAR0_INDEX] == 0x80000000U);
    assert(bar_write_count == 2U); /* probe write plus restore */

    /* Never probe a BAR while decode or bus mastering is active. */
    size = UINT64_MAX;
    config_space[1] = 0x0002U;
    unsigned writes_before = bar_write_count;
    assert(!pci_get_bar_size(&gpu, 0U, &size));
    assert(size == 0U && bar_write_count == writes_before);
    config_space[1] = 0x0001U;
    assert(!pci_get_bar_size(&gpu, 0U, &size));
    config_space[1] = 0x0004U;
    assert(!pci_get_bar_size(&gpu, 0U, &size));
    assert(bar_write_count == writes_before);

    /* 64-bit BAR: mask spans both dwords and both original halves are restored. */
    reset_config();
    config_space[TEST_BAR0_INDEX] = 0x80000004U;
    config_space[TEST_BAR0_INDEX + 1U] = 0x00000012U;
    bar_masks[0] = 0xF0000004U;
    bar_masks[1] = 0xFFFFFFFFU;
    assert(pci_get_bar_size(&gpu, 0U, &size));
    assert(size == 0x10000000ULL);
    assert(config_space[TEST_BAR0_INDEX] == 0x80000004U);
    assert(config_space[TEST_BAR0_INDEX + 1U] == 0x00000012U);
    assert(!pci_get_bar_size(&gpu, 1U, &size));
    assert(size == 0U);

    /* An unassigned 64-bit BAR can still be safely sized with decode disabled. */
    reset_config();
    config_space[TEST_BAR0_INDEX] = 0x00000004U;
    config_space[TEST_BAR0_INDEX + 1U] = 0U;
    bar_masks[0] = 0xFFE00004U; /* 2 MiB 64-bit memory BAR */
    bar_masks[1] = 0xFFFFFFFFU;
    assert(pci_get_bar_size(&gpu, 0U, &size));
    assert(size == 0x00200000ULL);
    assert(config_space[TEST_BAR0_INDEX] == 0x00000004U);
    assert(config_space[TEST_BAR0_INDEX + 1U] == 0U);

    /* Malformed masks are rejected after the original BAR is restored. */
    reset_config();
    config_space[TEST_BAR0_INDEX] = 0x80000000U;
    bar_masks[0] = 0xFFF0F000U;
    assert(!pci_get_bar_size(&gpu, 0U, &size));
    assert(size == 0U);
    assert(config_space[TEST_BAR0_INDEX] == 0x80000000U);
    assert(bar_write_count == 2U);

    /* Reserved memory BAR type, absent BAR and null output fail closed. */
    reset_config();
    config_space[TEST_BAR0_INDEX] = 0x80000006U;
    assert(!pci_get_bar_size(&gpu, 0U, &size));
    config_space[TEST_BAR0_INDEX] = 0U;
    assert(!pci_get_bar_size(&gpu, 0U, &size));
    assert(!pci_get_bar_size(&gpu, 0U, 0));

    return 0;
}
