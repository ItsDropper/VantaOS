#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdint.h>
#include "multiboot.h"

#define FS_MAX_NODES 64
#define FS_NAME_MAX 31
#define FS_PATH_MAX 128
#define FS_FILE_MAX 512

typedef enum
{
    FS_NODE_FILE = 1,
    FS_NODE_DIRECTORY = 2,
    FS_NODE_VIRTUAL = 3
} fs_node_type_t;

typedef struct
{
    uint32_t id;
    uint32_t parent;
    fs_node_type_t type;
    char name[FS_NAME_MAX + 1];
    char data[FS_FILE_MAX];
    uint32_t size;
} fs_node_t;

void filesystem_initialize(multiboot_info_t* mbd);

int filesystem_is_initialized(void);

int filesystem_lookup(const char* path);

const fs_node_t* filesystem_get_node(uint32_t id);

int filesystem_list(
    uint32_t directory_id,
    uint32_t* ids,
    unsigned int capacity
);

int filesystem_read(
    uint32_t file_id,
    char* buffer,
    unsigned int capacity
);

int filesystem_create_directory(const char* path);
int filesystem_create_file(const char* path);
int filesystem_write_file(const char* path, const char* data);
int filesystem_file_exists(const char* path);

int filesystem_create_directory(const char* path);
int filesystem_create_file(const char* path);
int filesystem_write_file(const char* path, const char* data);
int filesystem_file_exists(const char* path);

uint32_t filesystem_root(void);

#endif
