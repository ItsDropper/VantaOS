#include "shell_internal.h"

#include "heap.h"
#include "terminal.h"
#include "vfs.h"

#define SHELL_EXEC_MAX_IMAGE_SIZE (1024U * 1024U)

void shell_exec(const char* argument)
{
    if (!argument || !argument[0])
    {
        terminal_write("\nexec: usage: exec <elf-file>");
        return;
    }

    char path[FS_PATH_MAX];
    shell_resolve_path(argument, path);

    int node_id = vfs_lookup(path);
    if (node_id < 0)
    {
        terminal_write("\nexec: file not found: ");
        terminal_write(path);
        return;
    }

    const fs_node_t* node = vfs_get_node((uint32_t)node_id);
    if (!node || node->type != FS_NODE_FILE)
    {
        terminal_write("\nexec: path is not a regular file");
        return;
    }
    if (node->size < 64U || node->size > SHELL_EXEC_MAX_IMAGE_SIZE)
    {
        terminal_write("\nexec: ELF image size is outside the supported range");
        return;
    }

    int handle = vfs_open(path, VFS_OPEN_READ);
    if (handle < 0)
    {
        terminal_write("\nexec: could not open file");
        return;
    }

    size_t allocation_size = (size_t)node->size + 1U;
    char* image = (char*)kmalloc(allocation_size);
    if (!image)
    {
        vfs_close(handle);
        terminal_write("\nexec: not enough kernel memory to load image");
        return;
    }

    int bytes_read = vfs_read(handle, image, (unsigned int)allocation_size);
    vfs_close(handle);
    if (bytes_read < 0 || (uint32_t)bytes_read != node->size)
    {
        kfree(image);
        terminal_write("\nexec: incomplete file read");
        return;
    }

    int pid = process_create_user_elf(
        path, process_current_pid(), image, (size_t)bytes_read);
    kfree(image);

    if (pid <= 0)
    {
        terminal_write("\nexec: invalid ELF or process creation failed");
        return;
    }

    terminal_write("\nStarted ");
    terminal_write(path);
    terminal_write(" as PID ");
    shell_print_decimal((unsigned int)pid);
}
