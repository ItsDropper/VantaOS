#include "terminal.h"

#include <stddef.h>
#include <stdint.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

#define VGA_COLOR_WHITE_ON_BLACK 0x0F

#define TERMINAL_HISTORY_LINES 256

static volatile uint16_t* video_memory =
    (volatile uint16_t*)0xB8000;

/*
 * Terminal history is the source of truth.
 */
static uint16_t history[
    TERMINAL_HISTORY_LINES * VGA_WIDTH
];

static size_t history_start = 0;
static size_t history_count = 0;

/*
 * Current cursor position in terminal history.
 */
static size_t cursor_line = 0;
static size_t cursor_column = 0;

/*
 * Position where the current shell input begins.
 */
static size_t input_start_line = 0;
static size_t input_start_column = 0;

/*
 * Number of lines scrolled upward.
 *
 * 0 = live view.
 */
static size_t scroll_offset = 0;

static uint16_t terminal_entry(char c)
{
    return (uint16_t)c |
           ((uint16_t)VGA_COLOR_WHITE_ON_BLACK << 8);
}

static size_t history_physical_index(size_t logical_line)
{
    return
        (history_start + logical_line)
        % TERMINAL_HISTORY_LINES;
}

static uint16_t* history_line(size_t logical_line)
{
    size_t physical =
        history_physical_index(logical_line);

    return &history[
        physical * VGA_WIDTH
    ];
}

static void terminal_clear_line(size_t logical_line)
{
    uint16_t* line =
        history_line(logical_line);

    for (size_t x = 0; x < VGA_WIDTH; x++)
        line[x] = terminal_entry(' ');
}

static void terminal_add_line(void)
{
    size_t physical;

    if (history_count < TERMINAL_HISTORY_LINES)
    {
        physical =
            history_physical_index(history_count);

        history_count++;
    }
    else
    {
        physical = history_start;

        history_start =
            (history_start + 1)
            % TERMINAL_HISTORY_LINES;
    }

    for (size_t x = 0; x < VGA_WIDTH; x++)
    {
        history[
            physical * VGA_WIDTH + x
        ] = terminal_entry(' ');
    }

    cursor_line = history_count - 1;
    cursor_column = 0;
}

static size_t terminal_live_start(void)
{
    if (history_count > VGA_HEIGHT)
        return history_count - VGA_HEIGHT;

    return 0;
}

static void terminal_set_hardware_cursor(
    size_t row,
    size_t column
)
{
    size_t position =
        row * VGA_WIDTH + column;

    unsigned char low =
        (unsigned char)(position & 0xFF);

    unsigned char high =
        (unsigned char)(
            (position >> 8) & 0xFF
        );

    __asm__ volatile (
        "movb $0x0F, %%al\n"
        "outb %%al, $0x3D4\n"
        "movb %0, %%al\n"
        "outb %%al, $0x3D5\n"

        "movb $0x0E, %%al\n"
        "outb %%al, $0x3D4\n"
        "movb %1, %%al\n"
        "outb %%al, $0x3D5\n"
        :
        : "r"(low),
          "r"(high)
        : "al"
    );
}

static void terminal_render(void)
{
    if (history_count == 0)
    {
        for (size_t i = 0;
             i < VGA_WIDTH * VGA_HEIGHT;
             i++)
        {
            video_memory[i] =
                terminal_entry(' ');
        }

        return;
    }

    size_t live_start =
        terminal_live_start();

    size_t max_offset =
        live_start;

    if (scroll_offset > max_offset)
        scroll_offset = max_offset;

    size_t first_line =
        live_start - scroll_offset;

    for (size_t y = 0;
         y < VGA_HEIGHT;
         y++)
    {
        size_t logical_line =
            first_line + y;

        for (size_t x = 0;
             x < VGA_WIDTH;
             x++)
        {
            uint16_t value =
                terminal_entry(' ');

            if (logical_line < history_count)
            {
                uint16_t* line =
                    history_line(logical_line);

                value = line[x];
            }

            video_memory[
                y * VGA_WIDTH + x
            ] = value;
        }
    }

    /*
     * Only display the hardware cursor while
     * viewing the live terminal.
     */
    if (scroll_offset == 0)
    {
        size_t cursor_row;

        if (cursor_line >= live_start)
            cursor_row =
                cursor_line - live_start;
        else
            cursor_row = 0;

        if (cursor_row >= VGA_HEIGHT)
            cursor_row = VGA_HEIGHT - 1;

        terminal_set_hardware_cursor(
            cursor_row,
            cursor_column
        );
    }
}

static void terminal_clear_screen(void)
{
    history_start = 0;
    history_count = 0;

    cursor_line = 0;
    cursor_column = 0;

    input_start_line = 0;
    input_start_column = 0;

    scroll_offset = 0;

    for (size_t i = 0;
         i < VGA_WIDTH * VGA_HEIGHT;
         i++)
    {
        video_memory[i] =
            terminal_entry(' ');
    }

    terminal_add_line();
}

void terminal_scroll_up(void)
{
    if (history_count <= VGA_HEIGHT)
        return;

    size_t live_start =
        terminal_live_start();

    if (scroll_offset < live_start)
    {
        scroll_offset++;
        terminal_render();
    }
}

void terminal_scroll_down(void)
{
    if (scroll_offset > 0)
    {
        scroll_offset--;
        terminal_render();
    }
}

void terminal_initialize(void)
{
    terminal_clear_screen();

    terminal_write("VantaOS");
    terminal_putchar('\n');
}

void terminal_begin_input(void)
{
    if (history_count == 0)
        terminal_add_line();

    /*
     * Shell input always begins at the current
     * cursor position.
     */
    input_start_line = cursor_line;
    input_start_column = cursor_column;

    scroll_offset = 0;

    terminal_render();
}

void terminal_redraw_input(const char* text)
{
    if (history_count == 0)
        return;

    scroll_offset = 0;

    /*
     * Restore the terminal to the line where the
     * current shell prompt began.
     *
     * This removes any old command text and any
     * wrapped lines belonging to it.
     */
    if (input_start_line < history_count)
    {
        history_count =
            input_start_line + 1;

        cursor_line =
            input_start_line;

        cursor_column =
            input_start_column;

        uint16_t* line =
            history_line(input_start_line);

        for (size_t x = input_start_column;
             x < VGA_WIDTH;
             x++)
        {
            line[x] =
                terminal_entry(' ');
        }
    }

    /*
     * Write the new command.
     */
    while (*text)
    {
        terminal_putchar(*text);
        text++;
    }

    terminal_render();
}

void terminal_set_input_cursor(unsigned int offset)
{
    if (history_count == 0)
        return;

    /*
     * Convert an offset from the beginning of
     * the shell input into a terminal position.
     */
    size_t absolute_column =
        input_start_column + offset;

    size_t target_line =
        input_start_line +
        (absolute_column / VGA_WIDTH);

    size_t target_column =
        absolute_column % VGA_WIDTH;

    /*
     * Do not allow the cursor to point outside
     * the currently allocated terminal history.
     */
    if (target_line >= history_count)
    {
        target_line =
            history_count - 1;

        target_column =
            cursor_column;
    }

    cursor_line = target_line;
    cursor_column = target_column;

    terminal_render();
}

void terminal_putchar(char c)
{
    scroll_offset = 0;

    if (history_count == 0)
        terminal_add_line();

    if (c == '\n')
    {
        terminal_add_line();
        terminal_render();
        return;
    }

    if (c == '\r')
    {
        cursor_column = 0;
        terminal_render();
        return;
    }

    if (cursor_column >= VGA_WIDTH)
        terminal_add_line();

    uint16_t* line =
        history_line(cursor_line);

    line[cursor_column] =
        terminal_entry(c);

    cursor_column++;

    /*
     * Automatically wrap to the next line.
     */
    if (cursor_column >= VGA_WIDTH)
        terminal_add_line();

    terminal_render();
}

void terminal_backspace(void)
{
    if (history_count == 0)
        return;

    /*
     * Do not allow the cursor to move before
     * the shell prompt.
     */
    if (cursor_line < input_start_line)
        return;

    if (cursor_line == input_start_line &&
        cursor_column <= input_start_column)
    {
        return;
    }

    /*
     * Normal case.
     */
    if (cursor_column > 0)
    {
        cursor_column--;

        uint16_t* line =
            history_line(cursor_line);

        line[cursor_column] =
            terminal_entry(' ');

        terminal_render();

        return;
    }

    /*
     * Move backwards across a wrapped line.
     */
    if (cursor_line > input_start_line)
    {
        terminal_clear_line(cursor_line);

        history_count--;

        cursor_line--;

        cursor_column =
            VGA_WIDTH - 1;

        uint16_t* line =
            history_line(cursor_line);

        line[cursor_column] =
            terminal_entry(' ');

        terminal_render();
    }
}

void terminal_write(const char* text)
{
    while (*text)
    {
        terminal_putchar(*text);
        text++;
    }
}

void terminal_writestring(const char* text)
{
    terminal_write(text);
}

void terminal_write_hex(uint32_t value)
{
    const char* hex =
        "0123456789ABCDEF";

    terminal_write("0x");

    for (int i = 7; i >= 0; i--)
    {
        terminal_putchar(
            hex[(value >> (i * 4)) & 0xF]
        );
    }
}

size_t terminal_history_count(void)
{
    return history_count;
}

int terminal_history_line(
    size_t logical_line,
    char* buffer,
    size_t buffer_size
)
{
    if (!buffer || buffer_size == 0 || logical_line >= history_count)
        return 0;

    uint16_t* line = history_line(logical_line);
    size_t count = buffer_size - 1;

    if (count > VGA_WIDTH)
        count = VGA_WIDTH;

    for (size_t i = 0; i < count; i++)
        buffer[i] = (char)(line[i] & 0xFF);

    buffer[count] = 0;
    return 1;
}


size_t terminal_get_cursor_line(void)
{
    return cursor_line;
}

size_t terminal_get_cursor_column(void)
{
    return cursor_column;
}
