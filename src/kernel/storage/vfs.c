#include "vfs.h"

#define VFS_MAX_OPEN_FILES 32U

static vfs_file_t open_files[VFS_MAX_OPEN_FILES];
static int initialized;

void vfs_initialize(void)
{
    for (unsigned int i = 0; i < VFS_MAX_OPEN_FILES; i++)
    {
        open_files[i].node_id = 0;
        open_files[i].position = 0;
        open_files[i].flags = 0;
        open_files[i].used = 0;
    }

    initialized = filesystem_is_initialized();
}

int vfs_is_initialized(void)
{
    return initialized != 0;
}

int vfs_lookup(const char* path)
{
    if (!initialized)
        return -1;

    return filesystem_lookup(path);
}

const fs_node_t* vfs_get_node(uint32_t node_id)
{
    if (!initialized)
        return 0;

    return filesystem_get_node(node_id);
}

int vfs_list(
    uint32_t directory_id,
    uint32_t* ids,
    unsigned int capacity
)
{
    if (!initialized)
        return -1;

    return filesystem_list(
        directory_id,
        ids,
        capacity
    );
}

int vfs_open(
    const char* path,
    uint32_t flags
)
{
    if (!initialized || !path)
        return -1;

    int node_id =
        filesystem_lookup(path);

    if (node_id < 0)
        return -1;

    const fs_node_t* node =
        filesystem_get_node((uint32_t)node_id);

    if (!node ||
        (node->type != FS_NODE_FILE &&
         node->type != FS_NODE_VIRTUAL))
        return -1;

    for (unsigned int i = 0; i < VFS_MAX_OPEN_FILES; i++)
    {
        if (open_files[i].used)
            continue;

        open_files[i].node_id = (uint32_t)node_id;
        open_files[i].position = 0;
        open_files[i].flags = flags;
        open_files[i].used = 1;

        return (int)i;
    }

    return -1;
}

int vfs_read(
    int handle,
    char* buffer,
    unsigned int length
)
{
    if (!initialized ||
        handle < 0 ||
        handle >= (int)VFS_MAX_OPEN_FILES ||
        !buffer ||
        length == 0 ||
        !open_files[handle].used)
        return -1;

    const fs_node_t* node =
        filesystem_get_node(
            open_files[handle].node_id
        );

    if (!node ||
        (node->type != FS_NODE_FILE &&
         node->type != FS_NODE_VIRTUAL))
        return -1;

    char temporary[FS_FILE_MAX];

    int result =
        filesystem_read(
            node->id,
            temporary,
            sizeof(temporary)
        );

    if (result < 0)
        return -1;

    unsigned int size = (unsigned int)result;

    if (open_files[handle].position >= size)
        return 0;

    unsigned int available =
        size - open_files[handle].position;

    if (length > available)
        length = available;

    for (unsigned int i = 0; i < length; i++)
    {
        buffer[i] =
            temporary[
                open_files[handle].position + i
            ];
    }

    open_files[handle].position += length;

    return (int)length;
}

int vfs_write(
    int handle,
    const char* data,
    unsigned int length
)
{
    if (!initialized ||
        handle < 0 ||
        handle >= (int)VFS_MAX_OPEN_FILES ||
        !data ||
        !open_files[handle].used)
        return -1;

    const fs_node_t* node =
        filesystem_get_node(
            open_files[handle].node_id
        );

    if (!node || node->type != FS_NODE_FILE)
        return -1;

    if (length >= FS_FILE_MAX)
        return -1;

    char temporary[FS_FILE_MAX];
    unsigned int existing = 0;

    int result =
        filesystem_read(
            node->id,
            temporary,
            sizeof(temporary)
        );

    if (result < 0)
        return -1;

    existing = (unsigned int)result;

    if (open_files[handle].position > existing)
        return -1;

    if (open_files[handle].position + length >= FS_FILE_MAX)
        return -1;

    for (unsigned int i = 0; i < length; i++)
    {
        temporary[
            open_files[handle].position + i
        ] = data[i];
    }

    unsigned int final_size =
        existing;

    if (open_files[handle].position + length > final_size)
        final_size =
            open_files[handle].position + length;

    temporary[final_size] = 0;

    result =
        filesystem_write_file(
            node->name,
            temporary
        );

    /*
     * The current filesystem backend accepts absolute paths rather
     * than node IDs. Resolve the node path through its parent chain
     * once the persistent backend is introduced. For now, reject
     * writes here instead of pretending the node name is a path.
     */
    (void)result;
    return -1;
}

int vfs_close(int handle)
{
    if (!initialized ||
        handle < 0 ||
        handle >= (int)VFS_MAX_OPEN_FILES ||
        !open_files[handle].used)
        return -1;

    open_files[handle].used = 0;
    open_files[handle].node_id = 0;
    open_files[handle].position = 0;
    open_files[handle].flags = 0;

    return 0;
}

int vfs_create_file(const char* path)
{
    if (!initialized)
        return -1;

    return filesystem_create_file(path);
}

int vfs_create_directory(const char* path)
{
    if (!initialized)
        return -1;

    return filesystem_create_directory(path);
}
