/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "ahci_prdt.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    assert(sizeof(ahci_prdt_entry_t) == 16U);
    ahci_prdt_entry_t entries[3];
    memset(entries, 0xA5, sizeof(entries));
    uint32_t count = 99U;

    assert(ahci_prdt_build(0x1000U, 1U, entries, 3U, &count, 1));
    assert(count == 1U);
    assert(entries[0].data_base_low == 0x1000U);
    assert(entries[0].data_base_high == 0U);
    assert(entries[0].reserved == 0U);
    assert(entries[0].byte_count_and_interrupt == AHCI_PRDT_IOC);

    assert(ahci_prdt_build(0x2000U, AHCI_PRDT_MAX_BYTES,
                           entries, 3U, &count, 0));
    assert(count == 1U);
    assert(entries[0].byte_count_and_interrupt == AHCI_PRDT_DBC_MASK);

    assert(ahci_prdt_build(0x100002000ULL,
                           AHCI_PRDT_MAX_BYTES + 1U,
                           entries, 3U, &count, 1));
    assert(count == 2U);
    assert(entries[0].data_base_low == 0x2000U);
    assert(entries[0].data_base_high == 1U);
    assert(entries[0].byte_count_and_interrupt == AHCI_PRDT_DBC_MASK);
    assert(entries[1].data_base_low == 0x402000U);
    assert(entries[1].data_base_high == 1U);
    assert(entries[1].byte_count_and_interrupt == (AHCI_PRDT_IOC | 0U));

    memset(entries, 0xA5, sizeof(entries));
    ahci_prdt_entry_t before[3];
    memcpy(before, entries, sizeof(entries));
    assert(!ahci_prdt_build(0x1000U, AHCI_PRDT_MAX_BYTES + 1U,
                            entries, 1U, &count, 1));
    assert(count == 0U);
    assert(memcmp(entries, before, sizeof(entries)) == 0);
    assert(!ahci_prdt_build(0x1001U, 512U, entries, 3U, &count, 1));
    assert(count == 0U);
    assert(!ahci_prdt_build(0x1002U, 512U, entries, 3U, &count, 1));
    assert(count == 0U);
    assert(!ahci_prdt_build(0x1003U, 512U, entries, 3U, &count, 1));
    assert(count == 0U);
    assert(!ahci_prdt_build(0x1000U, 0U, entries, 3U, &count, 1));
    assert(count == 0U);
    assert(!ahci_prdt_build(UINT64_MAX - 3U, 8U,
                            entries, 3U, &count, 1));
    assert(count == 0U);
    assert(ahci_prdt_build(UINT64_MAX - 3U, 4U,
                           entries, 3U, &count, 0));
    assert(count == 1U);
    assert(entries[0].data_base_low == 0xFFFFFFFCU);
    assert(entries[0].data_base_high == 0xFFFFFFFFU);
    assert(entries[0].byte_count_and_interrupt == 3U);
    assert(!ahci_prdt_build(0x1000U, 512U, 0, 3U, &count, 1));
    assert(count == 0U);
    assert(!ahci_prdt_build(0x1000U, 512U, entries, 3U, 0, 1));

    puts("VANTAOS_AHCI_PRDT_OK");
    return 0;
}
