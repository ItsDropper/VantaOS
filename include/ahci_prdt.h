/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef AHCI_PRDT_H
#define AHCI_PRDT_H

#include <stddef.h>
#include <stdint.h>

#define AHCI_PRDT_MAX_BYTES (1U << 22)
#define AHCI_PRDT_DBC_MASK  0x003FFFFFU
#define AHCI_PRDT_IOC       0x80000000U

/* Hardware layout of one AHCI Physical Region Descriptor Table entry. */
typedef struct
{
    uint32_t data_base_low;
    uint32_t data_base_high;
    uint32_t reserved;
    uint32_t byte_count_and_interrupt;
} ahci_prdt_entry_t;

/*
 * Split one contiguous DMA buffer into PRDT entries. The caller must already
 * own a DMA-safe mapping and keep it alive until the HBA completes the command.
 * This helper validates descriptor geometry; it does not map memory, flush
 * caches, or establish device ownership.
 *
 * Failure leaves the entry array untouched and sets *entry_count_out to zero
 * when that output pointer is available.
 */
static inline int ahci_prdt_build(uint64_t dma_address, uint32_t byte_length,
                                  ahci_prdt_entry_t* entries,
                                  uint32_t entry_capacity,
                                  uint32_t* entry_count_out,
                                  int interrupt_on_completion)
{
    if (entry_count_out != 0)
        *entry_count_out = 0U;

    if (entries == 0 || entry_count_out == 0 || byte_length == 0U ||
        (dma_address & 3U) != 0U ||
        (uint64_t)(byte_length - 1U) > UINT64_MAX - dma_address)
        return 0;

    uint64_t required =
        ((uint64_t)byte_length + AHCI_PRDT_MAX_BYTES - 1U) /
        AHCI_PRDT_MAX_BYTES;
    if (required > entry_capacity)
        return 0;

    uint64_t current_address = dma_address;
    uint32_t remaining = byte_length;
    for (uint32_t i = 0U; i < (uint32_t)required; ++i)
    {
        uint32_t bytes = remaining > AHCI_PRDT_MAX_BYTES ?
            AHCI_PRDT_MAX_BYTES : remaining;

        entries[i].data_base_low = (uint32_t)current_address;
        entries[i].data_base_high = (uint32_t)(current_address >> 32);
        entries[i].reserved = 0U;
        entries[i].byte_count_and_interrupt = (bytes - 1U) &
            AHCI_PRDT_DBC_MASK;
        if (interrupt_on_completion != 0 && i + 1U == (uint32_t)required)
            entries[i].byte_count_and_interrupt |= AHCI_PRDT_IOC;

        remaining -= bytes;
        if (remaining != 0U)
            current_address += bytes;
    }

    *entry_count_out = (uint32_t)required;
    return 1;
}

#endif
