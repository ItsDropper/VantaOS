#ifndef ATA_H
#define ATA_H

#include <stdint.h>

void ata_initialize(void);
int ata_is_available(void);
int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer);

#endif
