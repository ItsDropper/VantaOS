#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>

void terminal_initialize(void);

void terminal_putchar(char c);

void terminal_backspace(void);

void terminal_write(const char* text);

void terminal_writestring(const char* text);

void terminal_write_hex(uint32_t value);

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

#endif