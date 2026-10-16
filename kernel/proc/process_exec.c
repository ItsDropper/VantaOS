#include "process.h"

#include "heap.h"
#include "vfs.h"

#define PROCESS_EXEC_MAX_IMAGE_SIZE (1024U * 1024U)

int process_spawn_user_elf_file(const char* path, uint32_t parent_pid,
                                uint32_t argc, const char* const argv[])
{
    if (!path || !path[0] || !argv || argc == 0U ||
        argc > PROCESS_USER_MAX_ARGS)
        return -22;

    size_t path_length = 0U;
    while (path_length < FS_PATH_MAX && path[path_length])
        path_length++;
    if (path_length == 0U || path_length == FS_PATH_MAX)
        return -22;

    int node_id = vfs_lookup(path);
    if (node_id < 0)
        return -2;

    const fs_node_t* node = vfs_get_node((uint32_t)node_id);
    if (!node || node->type != FS_NODE_FILE ||
        node->size < 64U || node->size > PROCESS_EXEC_MAX_IMAGE_SIZE)
        return -8;

    const size_t image_size = (size_t)node->size;
    int handle = vfs_open(path, VFS_OPEN_READ);
    if (handle < 0)
        return -2;

    char* image = (char*)kmalloc(image_size);
    if (!image)
    {
        (void)vfs_close(handle);
        return -12;
    }

    int bytes_read = vfs_read(handle, image, (unsigned int)image_size);
    int close_result = vfs_close(handle);
    if (bytes_read < 0 || (size_t)bytes_read != image_size ||
        close_result != 0)
    {
        kfree(image);
        return -5;
    }

    int pid = process_create_user_elf_args(
        path, parent_pid, image, image_size, argc, argv);
    kfree(image);
    return pid > 0 ? pid : -8;
}
