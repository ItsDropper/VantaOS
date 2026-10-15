#include "shell_internal.h"
#include "terminal.h"

void shell_clear_buffer(void)
{
    for (unsigned int i = 0;
         i < SHELL_BUFFER_SIZE;
         i++)
    {
        shell_buffer[i] = 0;
    }

    shell_length = 0;
    shell_cursor = 0;
}

void shell_prompt(void)
{
    terminal_write(">");

    terminal_begin_input();

    shell_clear_buffer();
}

void shell_show_prompt(void)
{
    shell_prompt();
}

int shell_string_equals(
    const char* a,
    const char* b
)
{
    unsigned int i = 0;

    while (a[i] != 0 && b[i] != 0)
    {
        if (a[i] != b[i])
            return 0;

        i++;
    }

    return a[i] == 0 &&
           b[i] == 0;
}

void shell_print_decimal(
    unsigned int value
)
{
    char buffer[16];
    unsigned int length = 0;

    if (value == 0)
    {
        terminal_putchar('0');
        return;
    }

    while (value > 0)
    {
        buffer[length++] =
            (char)('0' + (value % 10));

        value /= 10;
    }

    while (length > 0)
        terminal_putchar(buffer[--length]);
}

void shell_print_hex64(
    unsigned long long value
)
{
    const char* hex =
        "0123456789ABCDEF";

    terminal_write("0x");

    for (int i = 15; i >= 0; i--)
    {
        terminal_putchar(
            hex[(value >> (i * 4)) & 0xF]
        );
    }
}

void shell_copy_string(
    char* destination,
    const char* source
)
{
    unsigned int i = 0;

    while (source[i] != 0 &&
           i < SHELL_BUFFER_SIZE - 1)
    {
        destination[i] =
            source[i];

        i++;
    }

    destination[i] = 0;
}

void shell_load_buffer(
    const char* text
)
{
    shell_copy_string(
        shell_buffer,
        text
    );

    shell_length = 0;

    while (shell_buffer[shell_length] != 0 &&
           shell_length < SHELL_BUFFER_SIZE - 1)
    {
        shell_length++;
    }

    shell_cursor = shell_length;

    terminal_redraw_input(
        shell_buffer
    );

    terminal_set_input_cursor(
        shell_cursor
    );
}

void shell_insert_char(char c)
{
    if (shell_length >= SHELL_BUFFER_SIZE - 1)
        return;

    /*
     * Move everything after the cursor one position
     * to the right.
     */
    for (unsigned int i = shell_length;
         i > shell_cursor;
         i--)
    {
        shell_buffer[i] =
            shell_buffer[i - 1];
    }

    shell_buffer[shell_cursor] = c;

    shell_length++;
    shell_cursor++;

    terminal_redraw_input(
        shell_buffer
    );

    terminal_set_input_cursor(
        shell_cursor
    );
}

void shell_backspace(void)
{
    if (shell_cursor == 0)
        return;

    /*
     * Remove the character immediately before
     * the cursor.
     */
    for (unsigned int i = shell_cursor - 1;
         i < shell_length - 1;
         i++)
    {
        shell_buffer[i] =
            shell_buffer[i + 1];
    }

    shell_length--;

    shell_buffer[shell_length] = 0;

    shell_cursor--;

    terminal_redraw_input(
        shell_buffer
    );

    terminal_set_input_cursor(
        shell_cursor
    );
}

void shell_delete(void)
{
    if (shell_cursor >= shell_length)
        return;

    for (unsigned int i = shell_cursor;
         i < shell_length - 1;
         i++)
    {
        shell_buffer[i] =
            shell_buffer[i + 1];
    }

    shell_length--;

    shell_buffer[shell_length] = 0;

    terminal_redraw_input(
        shell_buffer
    );

    terminal_set_input_cursor(
        shell_cursor
    );
}

void shell_move_left(void)
{
    if (shell_cursor == 0)
        return;

    shell_cursor--;

    terminal_set_input_cursor(
        shell_cursor
    );
}

void shell_move_right(void)
{
    if (shell_cursor >= shell_length)
        return;

    shell_cursor++;

    terminal_set_input_cursor(
        shell_cursor
    );
}

void shell_move_home(void)
{
    shell_cursor = 0;

    terminal_set_input_cursor(
        shell_cursor
    );
}

void shell_move_end(void)
{
    shell_cursor = shell_length;

    terminal_set_input_cursor(
        shell_cursor
    );
}

void shell_save_draft(void)
{
    shell_copy_string(
        shell_history_draft,
        shell_buffer
    );

    shell_history_draft_length =
        shell_length;
}

void shell_restore_draft(void)
{
    shell_load_buffer(
        shell_history_draft
    );
}

void shell_add_history(void)
{
    if (shell_length == 0)
        return;

    /*
     * Do not store duplicate consecutive commands.
     */
    if (shell_history_count > 0)
    {
        if (shell_string_equals(
                shell_history[
                    shell_history_count - 1
                ],
                shell_buffer))
        {
            shell_history_position =
                shell_history_count;

            return;
        }
    }

    if (shell_history_count <
        SHELL_HISTORY_SIZE)
    {
        shell_copy_string(
            shell_history[
                shell_history_count
            ],
            shell_buffer
        );

        shell_history_count++;
    }
    else
    {
        /*
         * Drop the oldest command.
         */
        for (unsigned int i = 1;
             i < SHELL_HISTORY_SIZE;
             i++)
        {
            shell_copy_string(
                shell_history[i - 1],
                shell_history[i]
            );
        }

        shell_copy_string(
            shell_history[
                SHELL_HISTORY_SIZE - 1
            ],
            shell_buffer
        );
    }

    shell_history_position =
        shell_history_count;
}

void shell_history_up(void)
{
    if (shell_history_count == 0)
        return;

    /*
     * Save the current unfinished command the first
     * time we enter history.
     */
    if (shell_history_position ==
        shell_history_count)
    {
        shell_save_draft();
    }

    if (shell_history_position > 0)
        shell_history_position--;
    else
        return;

    shell_load_buffer(
        shell_history[
            shell_history_position
        ]
    );
}

void shell_history_down(void)
{
    if (shell_history_count == 0)
        return;

    if (shell_history_position <
        shell_history_count - 1)
    {
        shell_history_position++;

        shell_load_buffer(
            shell_history[
                shell_history_position
            ]
        );

        return;
    }

    /*
     * Moving down from the newest command returns
     * to the unfinished draft.
     */
    shell_history_position =
        shell_history_count;

    shell_restore_draft();
}
