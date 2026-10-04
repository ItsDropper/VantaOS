#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>
#include <stddef.h>
#include <stddef.h>

void terminal_initialize(void);
void terminal_reset(void);

void terminal_putchar(char c);

void terminal_backspace(void);

void terminal_write(const char* text);

void terminal_writestring(const char* text);

void terminal_write_hex(uint64_t value);

void terminal_scroll_up(void);

void terminal_scroll_down(void);

/*
 * Begin tracking a new shell input line.
 */
void terminal_begin_input(void);

/*
 * Completely redraw the current shell input.
 */
void terminal_redraw_input(const char* text);

/*
 * Move the hardware cursor to a position inside
 * the current shell input.
 */
void terminal_set_input_cursor(unsigned int offset);

void terminal_render_for_desktop(void);

size_t terminal_history_count(void);
int terminal_history_line(size_t line, char* buffer, size_t buffer_size);

size_t terminal_get_cursor_line(void);
size_t terminal_get_cursor_column(void);
size_t terminal_get_scroll_offset(void);

#endif