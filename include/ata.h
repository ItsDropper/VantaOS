/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef ATA_H
#define ATA_H

#include <stdint.h>

void ata_initialize(void);
int ata_is_available(void);
int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer);
/* Low-level controller I/O. Filesystem and general block consumers must use
   block_device_read/write/flush so capacity and policy checks are enforced. */
int ata_write_sectors(uint32_t lba, uint8_t count, const void* buffer);

uint8_t ata_status(void);
uint8_t ata_error(void);
uint8_t ata_signature_mid(void);
uint8_t ata_signature_high(void);

#endif
