#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include "filesystem.h"

typedef struct
{
    uint32_t node_id;
    uint32_t position;
    uint32_t flags;
    int used;
} vfs_file_t;

void vfs_initialize(void);
int vfs_is_initialized(void);

int vfs_lookup(const char* path);
const fs_node_t* vfs_get_node(uint32_t node_id);

int vfs_list(
    uint32_t directory_id,
    uint32_t* ids,
    unsigned int capacity
);

int vfs_open(
    const char* path,
    uint32_t flags
);

int vfs_read(
    int handle,
    char* buffer,
    unsigned int length
);

int vfs_write(
    int handle,
    const char* data,
    unsigned int length
);

int vfs_close(int handle);

int vfs_create_file(const char* path);
int vfs_create_directory(const char* path);

#endif
