/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "ata_lba28.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    const uint64_t capacity = 0x10000000ULL;
    assert(ata_lba28_request_valid(0U, 1U, capacity));
    assert(ata_lba28_request_valid(0x0FFFFFFFU, 1U, capacity));
    assert(ata_lba28_request_valid(0x0FFFFFF0U, 16U, capacity));
    assert(ata_lba28_request_valid(100U, 255U, capacity));
    assert(!ata_lba28_request_valid(0U, 0U, capacity));
    assert(!ata_lba28_request_valid(0U, 256U, capacity));
    assert(!ata_lba28_request_valid(0x10000000ULL, 1U, capacity));
    assert(!ata_lba28_request_valid(0x0FFFFFFFU, 2U, capacity));
    assert(!ata_lba28_request_valid(100U, 2U, 101U));
    assert(!ata_lba28_request_valid(101U, 1U, 101U));
    assert(!ata_lba28_request_valid(UINT64_MAX, 1U, capacity));
    puts("VANTAOS_ATA_LBA28_BOUNDS_OK");
    return 0;
}
