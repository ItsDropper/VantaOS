/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef AHCI_PCI_H
#define AHCI_PCI_H

#include <stdint.h>

/* AHCI HBA registers include the port register block at 0x100 + 32 * 0x80. */
#define AHCI_ABAR_MIN_SIZE 0x1100ULL
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
        size < AHCI_ABAR_MIN_SIZE ||
        (base & (AHCI_ABAR_ALIGNMENT - 1ULL)) != 0U ||
        (size & (size - 1ULL)) != 0U ||
        (base & (size - 1ULL)) != 0U ||
        size - 1ULL > UINT64_MAX - base)
        return 0;

    if (is_64bit == 0 && base + size - 1ULL > UINT32_MAX)
        return 0;

    return 1;
}

#endif
