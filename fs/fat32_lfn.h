#ifndef FAT32_LFN_H
#define FAT32_LFN_H

#include <stdint.h>

#define FAT32_LFN_MAX_ENTRIES 20U
#define FAT32_LFN_CHARS (FAT32_LFN_MAX_ENTRIES * 13U)

typedef struct
{
    uint16_t chars[FAT32_LFN_CHARS];
    int active;
    uint8_t expected_checksum;
    uint8_t total;
    uint32_t seen;
} fat32_lfn_state_t;

void fat32_lfn_reset(fat32_lfn_state_t* state);
int fat32_lfn_consume(const uint8_t* entry, fat32_lfn_state_t* state);
uint8_t fat32_lfn_checksum(const uint8_t* short_entry);
int fat32_lfn_is_complete(const fat32_lfn_state_t* state);
int fat32_lfn_decode(const fat32_lfn_state_t* state, char* name);

#endif
