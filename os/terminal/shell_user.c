/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "shell_internal.h"

#include "terminal.h"

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

    const char* user_argv[PROCESS_USER_MAX_ARGS];
    user_argv[0] = path;
    for (uint32_t i = 1U; i < argc; i++)
        user_argv[i] = parsed_argv[i];

    int pid = process_spawn_user_elf_file(
        path, process_current_pid(), argc, user_argv);
    if (pid <= 0)
    {
        if (pid == -2)
            terminal_write("\nexec: file not found or could not be opened");
        else if (pid == -5)
            terminal_write("\nexec: incomplete file read");
        else if (pid == -12)
            terminal_write("\nexec: not enough kernel memory to load image");
        else
            terminal_write("\nexec: invalid ELF or process creation failed");
        return;
    }

    terminal_write("\nStarted ");
    terminal_write(path);
    terminal_write(" as PID ");
    shell_print_decimal((unsigned int)pid);
}
