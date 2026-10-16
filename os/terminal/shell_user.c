#include "shell_internal.h"

#include "heap.h"
#include "terminal.h"
#include "vfs.h"

#define SHELL_EXEC_MAX_IMAGE_SIZE (1024U * 1024U)

static int parse_exec_arguments(const char* input, char* storage,
                                size_t storage_size, const char** argv,
                                uint32_t argv_capacity, uint32_t* argc_out)
{
    if (!input || !storage || storage_size == 0U || !argv || !argc_out)
        return 0;

    size_t input_length = 0U;
    while (input[input_length])
    {
        if (input_length + 1U >= storage_size)
            return 0;
        storage[input_length] = input[input_length];
        input_length++;
    }
    storage[input_length] = '\0';

    char* read = storage;
    char* write = storage;
    uint32_t argc = 0U;
    while (*read)
    {
        while (*read == ' ' || *read == '\t')
            read++;
        if (!*read)
            break;
        if (argc >= argv_capacity)
            return 0;

        char quote = 0;
        if (*read == '"' || *read == '\'')
            quote = *read++;

        argv[argc++] = write;
        while (*read)
        {
            if (quote && *read == quote)
                break;
            if (!quote && (*read == ' ' || *read == '\t'))
                break;
            if (*read == '\\')
            {
                if (!read[1])
                    return 0;
                read++;
            }
            *write++ = *read++;
        }

        if (quote)
        {
            if (*read != quote)
                return 0;
            read++;
            if (*read && *read != ' ' && *read != '\t')
                return 0;
        }
        else if (*read == ' ' || *read == '\t')
        {
            /* Advance before writing NUL when read and write alias. */
            read++;
        }
        *write++ = '\0';
    }

    *argc_out = argc;
    return argc != 0U;
}

void shell_exec(const char* argument)
{
    if (!argument || !argument[0])
    {
        terminal_write("\nexec: usage: exec <elf-file> [args...]");
        return;
    }

    char argument_storage[SHELL_BUFFER_SIZE];
    const char* parsed_argv[PROCESS_USER_MAX_ARGS];
    uint32_t argc = 0U;
    if (!parse_exec_arguments(argument, argument_storage,
                              sizeof(argument_storage), parsed_argv,
                              PROCESS_USER_MAX_ARGS, &argc))
    {
        terminal_write("\nexec: malformed command line or too many arguments");
        return;
    }

    char path[FS_PATH_MAX];
    shell_resolve_path(parsed_argv[0], path);

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

    const char* user_argv[PROCESS_USER_MAX_ARGS];
    user_argv[0] = path;
    for (uint32_t i = 1U; i < argc; i++)
        user_argv[i] = parsed_argv[i];

    int pid = process_create_user_elf_args(
        path, process_current_pid(), image, (size_t)bytes_read,
        argc, user_argv);
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
