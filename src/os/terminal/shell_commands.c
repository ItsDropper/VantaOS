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
    else if (shell_string_equals(
                 shell_buffer,
                 "ps"))
    {
        shell_ps();
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
