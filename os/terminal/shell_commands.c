/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "shell_internal.h"
#include "terminal.h"

void shell_execute(void)
{
    shell_buffer[shell_length] = 0;

    /*
     * Move to the end of the command before
     * executing it. This matters when the user
     * presses Enter while the cursor is in the
     * middle of the command.
     */
    terminal_set_input_cursor(
        shell_length
    );

    if (shell_length == 0)
    {
        terminal_putchar('\n');

        shell_show_prompt();

        return;
    }

    shell_add_history();

    if (shell_execute_extended(shell_buffer))
    {
        terminal_putchar('\n');
        shell_clear_buffer();
        shell_history_position = shell_history_count;
        shell_show_prompt();
        return;
    }

    if (shell_string_equals(
            shell_buffer,
            "help"))
    {
        shell_help();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "clear"))
    {
        terminal_initialize();

        shell_clear_buffer();

        shell_show_prompt();

        return;
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "about"))
    {
        shell_about();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "specs"))
    {
        shell_specs();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "boot"))
    {
        shell_boot();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "uptime"))
    {
        shell_uptime();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "mem"))
    {
        shell_mem();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "heap"))
    {
        shell_heap();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "history"))
    {
        shell_show_history();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "fault"))
    {
        shell_fault();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "reboot"))
    {
        shell_reboot();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "pwd"))
    {
        shell_pwd();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "ls"))
    {
        shell_ls(0);
    }
    else if (shell_buffer[0] == 'l' &&
             shell_buffer[1] == 's' &&
             shell_buffer[2] == ' ')
    {
        shell_ls(&shell_buffer[3]);
    }
    else if (shell_buffer[0] == 'c' &&
             shell_buffer[1] == 'd' &&
             shell_buffer[2] == 0)
    {
        shell_cd(0);
    }
    else if (shell_buffer[0] == 'c' &&
             shell_buffer[1] == 'd' &&
             shell_buffer[2] == ' ')
    {
        shell_cd(&shell_buffer[3]);
    }
    else if (shell_buffer[0] == 'c' &&
             shell_buffer[1] == 'a' &&
             shell_buffer[2] == 't' &&
             shell_buffer[3] == ' ')
    {
        shell_cat(&shell_buffer[4]);
    }
    else if (shell_string_equals(shell_buffer, "exec"))
    {
        shell_exec(0);
    }
    else if (shell_buffer[0] == 'e' &&
             shell_buffer[1] == 'x' &&
             shell_buffer[2] == 'e' &&
             shell_buffer[3] == 'c' &&
             shell_buffer[4] == ' ')
    {
        shell_exec(&shell_buffer[5]);
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "ps"))
    {
        shell_ps();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "storage"))
    {
        shell_storage();
    }
    else if (shell_buffer[0] == 'm' &&
             shell_buffer[1] == 'k' &&
             shell_buffer[2] == 'd' &&
             shell_buffer[3] == 'i' &&
             shell_buffer[4] == 'r' &&
             shell_buffer[5] == ' ')
    {
        shell_mkdir(&shell_buffer[6]);
    }
    else if (shell_buffer[0] == 't' &&
             shell_buffer[1] == 'o' &&
             shell_buffer[2] == 'u' &&
             shell_buffer[3] == 'c' &&
             shell_buffer[4] == 'h' &&
             shell_buffer[5] == ' ')
    {
        shell_touch(&shell_buffer[6]);
    }
    else if (shell_buffer[0] == 'w' &&
             shell_buffer[1] == 'r' &&
             shell_buffer[2] == 'i' &&
             shell_buffer[3] == 't' &&
             shell_buffer[4] == 'e' &&
             shell_buffer[5] == ' ')
    {
        shell_write_file(&shell_buffer[6]);
    }
    else if (shell_buffer[0] == 'e' &&
             shell_buffer[1] == 'c' &&
             shell_buffer[2] == 'h' &&
             shell_buffer[3] == 'o' &&
             shell_buffer[4] == ' ')
    {
        terminal_putchar('\n');

        terminal_write(
            &shell_buffer[5]
        );
    }
    else
    {
        terminal_write(
            "\nUnknown command: "
        );

        terminal_write(
            shell_buffer
        );
    }

    terminal_putchar('\n');

    shell_clear_buffer();

    shell_history_position =
        shell_history_count;

    shell_show_prompt();
}

int shell_execute_kernel_command(const char* command, char* output,
                                 size_t output_capacity)
{
    if (!command || !output || output_capacity == 0U)
        return -22;

    size_t length = 0U;
    while (command[length] != '\0')
    {
        if (length >= SHELL_BUFFER_SIZE - 1U)
            return -22;
        length++;
    }
    if (length == 0U)
        return -22;

    char saved_buffer[SHELL_BUFFER_SIZE];
    for (size_t i = 0U; i < sizeof(saved_buffer); i++)
        saved_buffer[i] = shell_buffer[i];
    unsigned int saved_length = shell_length;
    unsigned int saved_cursor = shell_cursor;

    for (size_t i = 0U; i < length; i++)
        shell_buffer[i] = command[i];
    shell_buffer[length] = '\0';
    shell_length = (unsigned int)length;
    shell_cursor = shell_length;

    terminal_capture_begin(output, output_capacity);
    shell_execute();
    size_t captured = terminal_capture_end();

    for (size_t i = 0U; i < sizeof(saved_buffer); i++)
        shell_buffer[i] = saved_buffer[i];
    shell_length = saved_length;
    shell_cursor = saved_cursor;
    return captured > 0U ? (int)captured : 0;
}
