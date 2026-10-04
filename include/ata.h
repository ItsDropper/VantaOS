#ifndef ATA_H
#define ATA_H

#include <stdint.h>

void ata_initialize(void);
int ata_is_available(void);
int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer);

uint8_t ata_status(void);
uint8_t ata_error(void);
uint8_t ata_signature_mid(void);
uint8_t ata_signature_high(void);

#endif
