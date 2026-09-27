#include "shell_internal.h"
#include "terminal.h"
#include "vfs.h"

void shell_copy_path(char* destination, const char* source)
{
    unsigned int i = 0;

    while (source[i] != 0 && i < FS_PATH_MAX - 1)
    {
        destination[i] = source[i];
        i++;
    }

    destination[i] = 0;
}

void shell_append_path(
    char* destination,
    const char* source
)
{
    unsigned int length = 0;

    while (destination[length] != 0 &&
           length < FS_PATH_MAX - 1)
        length++;

    unsigned int i = 0;

    while (source[i] != 0 &&
           length < FS_PATH_MAX - 1)
        destination[length++] = source[i++];

    destination[length] = 0;
}

int shell_resolve_path(
    const char* argument,
    char* resolved
)
{
    if (argument == 0 || argument[0] == 0)
    {
        shell_copy_path(resolved, shell_cwd);
        return 1;
    }

    if (argument[0] == '/')
    {
        shell_copy_path(resolved, argument);
        return 1;
    }

    shell_copy_path(resolved, shell_cwd);

    if (resolved[1] != 0)
        shell_append_path(resolved, "/");
    else
        shell_append_path(resolved, "");

    shell_append_path(resolved, argument);

    return 1;
}

void shell_ls(const char* argument)
{
    char path[FS_PATH_MAX];
    shell_resolve_path(argument, path);

    int directory_id =
        vfs_lookup(path);

    if (directory_id < 0)
    {
        terminal_write("\nls: path not found: ");
        terminal_write(argument ? argument : "");
        terminal_putchar('\n');
        return;
    }

    const fs_node_t* directory =
        vfs_get_node((uint32_t)directory_id);

    if (directory == 0 ||
        directory->type != FS_NODE_DIRECTORY)
    {
        terminal_write("\nls: not a directory\n");
        return;
    }

    uint32_t ids[FS_MAX_NODES];
    int count = vfs_list(
        (uint32_t)directory_id,
        ids,
        FS_MAX_NODES
    );

    terminal_write("\n");

    for (int i = 0; i < count; i++)
    {
        const fs_node_t* node =
            filesystem_get_node(ids[i]);

        if (node == 0)
            continue;

        terminal_write(node->name);

        if (node->type == FS_NODE_DIRECTORY)
            terminal_putchar('/');

        terminal_write("  ");
    }

    terminal_putchar('\n');
}

void shell_cat(const char* argument)
{
    if (argument == 0 || argument[0] == 0)
    {
        terminal_write("\ncat: missing file\n");
        return;
    }

    char path[FS_PATH_MAX];
    shell_resolve_path(argument, path);

    int file_id =
        filesystem_lookup(path);

    if (file_id < 0)
    {
        terminal_write("\ncat: file not found: ");
        terminal_write(argument);
        terminal_putchar('\n');
        return;
    }

    const fs_node_t* file =
        filesystem_get_node((uint32_t)file_id);

    if (file == 0 ||
        (file->type != FS_NODE_FILE &&
         file->type != FS_NODE_VIRTUAL))
    {
        terminal_write("\ncat: not a file\n");
        return;
    }

    char buffer[FS_FILE_MAX];

    if (filesystem_read(
            (uint32_t)file_id,
            buffer,
            FS_FILE_MAX) < 0)
    {
        terminal_write("\ncat: read failed\n");
        return;
    }

    terminal_putchar('\n');
    terminal_write(buffer);

    if (file->size == 0 ||
        buffer[file->size - 1] != '\n')
        terminal_putchar('\n');
}

void shell_pwd(void)
{
    terminal_write("\n");
    terminal_write(shell_cwd);
    terminal_putchar('\n');
}

void shell_cd(const char* argument)
{
    char path[FS_PATH_MAX];

    if (argument == 0 || argument[0] == 0)
        shell_copy_path(path, "/");
    else
        shell_resolve_path(argument, path);

    int directory_id =
        filesystem_lookup(path);

    if (directory_id < 0)
    {
        terminal_write("\ncd: path not found: ");
        terminal_write(argument ? argument : "");
        terminal_putchar('\n');
        return;
    }

    const fs_node_t* directory =
        filesystem_get_node((uint32_t)directory_id);

    if (directory == 0 ||
        directory->type != FS_NODE_DIRECTORY)
    {
        terminal_write("\ncd: not a directory\n");
        return;
    }

    if (path[0] == 0)
        shell_copy_path(shell_cwd, "/");
    else
        shell_copy_path(shell_cwd, path);

    if (shell_cwd[1] == 0)
        return;

    unsigned int length = 0;
    while (shell_cwd[length] != 0)
        length++;

    while (length > 1 && shell_cwd[length - 1] == '/')
    {
        shell_cwd[length - 1] = 0;
        length--;
    }
}

void shell_mkdir(const char* argument)
{
    if (argument == 0 || argument[0] == 0)
    {
        terminal_write("\nmkdir: missing path");
        return;
    }

    char path[FS_PATH_MAX];
    shell_resolve_path(argument, path);

    if (vfs_create_directory(path) < 0)
    {
        terminal_write("\nmkdir: cannot create ");
        terminal_write(argument);
        return;
    }

    terminal_write("\ncreated directory ");
    terminal_write(path);
}

void shell_touch(const char* argument)
{
    if (argument == 0 || argument[0] == 0)
    {
        terminal_write("\ntouch: missing path");
        return;
    }

    char path[FS_PATH_MAX];
    shell_resolve_path(argument, path);

    int existing = filesystem_lookup(path);

    if (existing >= 0)
    {
        const fs_node_t* node =
            filesystem_get_node((uint32_t)existing);

        if (node != 0 && node->type == FS_NODE_FILE)
        {
            terminal_write("\ntouch: ");
            terminal_write(path);
            terminal_write(" already exists");
            return;
        }

        terminal_write("\ntouch: path already exists");
        return;
    }

    if (vfs_create_file(path) < 0)
    {
        terminal_write("\ntouch: cannot create ");
        terminal_write(argument);
        return;
    }

    terminal_write("\ncreated file ");
    terminal_write(path);
}

void shell_write_file(const char* arguments)
{
    if (arguments == 0 || arguments[0] == 0)
    {
        terminal_write("\nwrite: usage: write <file> <text>");
        return;
    }

    char path[FS_PATH_MAX];
    unsigned int i = 0;

    while (arguments[i] != 0 &&
           arguments[i] != ' ' &&
           i < FS_PATH_MAX - 1)
    {
        path[i] = arguments[i];
        i++;
    }

    path[i] = 0;

    if (path[0] == 0 || arguments[i] == 0)
    {
        terminal_write("\nwrite: usage: write <file> <text>");
        return;
    }

    while (arguments[i] == ' ')
        i++;

    if (arguments[i] == 0)
    {
        terminal_write("\nwrite: missing text");
        return;
    }

    char resolved[FS_PATH_MAX];
    shell_resolve_path(path, resolved);

    if (!filesystem_file_exists(resolved))
    {
        terminal_write("\nwrite: file does not exist");
        return;
    }

    int result =
        filesystem_write_file(resolved, &arguments[i]);

    if (result == -2)
    {
        terminal_write("\nwrite: file is too large");
        return;
    }

    if (result < 0)
    {
        terminal_write("\nwrite: write failed");
        return;
    }

    terminal_write("\nwrote ");
    shell_print_decimal((unsigned int)result);
    terminal_write(" bytes");
}
