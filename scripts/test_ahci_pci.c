/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "ahci_pci.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    assert(ahci_pci_bar_sizing_allowed(0x0000U));
    assert(ahci_pci_bar_sizing_allowed(0x0008U));
    assert(!ahci_pci_bar_sizing_allowed(0x0001U));
    assert(!ahci_pci_bar_sizing_allowed(0x0002U));
    assert(!ahci_pci_bar_sizing_allowed(0x0004U));
    assert(!ahci_pci_bar_sizing_allowed(0x0007U));
    assert(ahci_pci_abar_valid(0xFEBF0000ULL, 0x2000ULL, 0, 0));
    assert(ahci_pci_abar_valid(0x100000000ULL, 0x2000ULL, 0, 1));

    assert(!ahci_pci_abar_valid(0U, 0x2000ULL, 0, 0));
    assert(!ahci_pci_abar_valid(0xFEBF0000ULL, 0x2000ULL, 1, 0));
    assert(!ahci_pci_abar_valid(0xFEBF0000ULL, 0x2000ULL, 0, 2));
    assert(ahci_pci_abar_valid(0xFEBF0000ULL, 0x1000ULL, 0, 0));
    assert(ahci_pci_abar_fits_ports(0x180ULL, 0U));
    assert(!ahci_pci_abar_fits_ports(0x100ULL, 0U));
    assert(ahci_pci_abar_fits_ports(0x1100ULL, 31U));
    assert(!ahci_pci_abar_fits_ports(0x1080ULL, 31U));
    assert(!ahci_pci_abar_valid(0xFEBF0000ULL, 0x20ULL, 0, 0));
    assert(!ahci_pci_abar_valid(0xFEBF0000ULL, 0x1800ULL, 0, 0));
    assert(!ahci_pci_abar_valid(0xFEBF0001ULL, 0x2000ULL, 0, 0));
    assert(!ahci_pci_abar_valid(0xFEBF1000ULL, 0x2000ULL, 0, 0));
    assert(!ahci_pci_abar_valid(UINT64_MAX - 0x7FFULL, 0x2000ULL, 0, 1));
    assert(!ahci_pci_abar_valid(0x100000000ULL, 0x2000ULL, 0, 0));
    assert(!ahci_pci_abar_valid(0xFFFFE000ULL, 0x4000ULL, 0, 0));

    puts("VANTAOS_AHCI_PCI_OK");
    return 0;
}
