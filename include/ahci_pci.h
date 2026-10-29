/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef AHCI_PCI_H
#define AHCI_PCI_H

#include <stdint.h>

/* Map only global registers until CAP reveals the HBA's implemented port span. */
#define AHCI_ABAR_PROBE_SIZE 0x40ULL
#define AHCI_ABAR_PORT_BASE 0x100ULL
#define AHCI_ABAR_PORT_SIZE 0x80ULL
#define AHCI_ABAR_ALIGNMENT 0x400ULL

/* BAR sizing writes all ones temporarily; decode and bus mastering must be off. */
static inline int ahci_pci_bar_sizing_allowed(uint16_t command)
{
    return (command & 0x0007U) == 0U;
}

/*
 * Validate metadata already obtained from PCI configuration space.
 * The caller remains responsible for obtaining a trustworthy BAR size while
 * decode is disabled and for mapping only the validated resource range.
 */
static inline int ahci_pci_abar_valid(uint64_t base, uint64_t size,
                                      int is_io, int is_64bit)
{
    if (base == 0U || is_io != 0 || (is_64bit != 0 && is_64bit != 1) ||
        size < AHCI_ABAR_PROBE_SIZE ||
        (size & (size - 1ULL)) != 0U ||
        size - 1ULL > UINT64_MAX - base)
        return 0;

    if ((base & (AHCI_ABAR_ALIGNMENT - 1ULL)) != 0U ||
        (base & (size - 1ULL)) != 0U)
        return 0;

    if (is_64bit == 0 && base + size - 1ULL > UINT32_MAX)
        return 0;

    return 1;
}

/* CAP.NP encodes the maximum port index; reserve its full register block. */
static inline int ahci_pci_abar_fits_ports(uint64_t size, uint32_t cap)
{
    uint64_t port_count = (uint64_t)(cap & 0x1FU) + 1ULL;
    uint64_t required = AHCI_ABAR_PORT_BASE +
                        port_count * AHCI_ABAR_PORT_SIZE;
    return size >= required;
}

#endif
