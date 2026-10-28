/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef ATA_LBA28_H
#define ATA_LBA28_H

#include <stdint.h>

/* Validate a legacy ATA PIO request before programming its 28-bit LBA and
 * 8-bit sector-count registers. This driver deliberately caps requests at
 * 255 sectors; zero has ATA-specific semantics and is not accepted here. */
static inline int ata_lba28_request_valid(uint64_t lba, uint32_t count,
                                          uint64_t capacity_sectors)
{
    if (count == 0U || count > 255U || lba > 0x0FFFFFFFU ||
        lba >= capacity_sectors)
        return 0;
    if ((uint64_t)count > capacity_sectors - lba)
        return 0;
    if ((uint64_t)count - 1U > 0x0FFFFFFFU - lba)
        return 0;
    return 1;
}

#endif
