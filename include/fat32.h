#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>

typedef struct
{
    char name[256];
    uint8_t directory;
    uint32_t first_cluster;
    uint32_t size;
} fat32_dirent_t;

void fat32_initialize(void);
int fat32_is_mounted(void);

uint32_t fat32_root_cluster(void);

int fat32_list_directory(
    uint32_t cluster,
    fat32_dirent_t* entries,
    unsigned int capacity
);

int fat32_read_file(
    uint32_t first_cluster,
    uint32_t size,
    char* buffer,
    unsigned int capacity
);
int fat32_read_file_range(uint32_t first_cluster, uint32_t size,
                          uint32_t offset, char* buffer, uint32_t length);

int fat32_create_file(const char* path);
int fat32_write_file(const char* path, const char* data, uint32_t size);
int fat32_write_file_range(const char* path, uint32_t offset,
                           const char* data, uint32_t length);

#endif
