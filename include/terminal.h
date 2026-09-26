#ifndef TERMINAL_H
#define TERMINAL_H

void terminal_initialize(void);

void terminal_putchar(char c);

void terminal_backspace(void);

void terminal_write(const char* text);

void terminal_write_hex(unsigned int value);

#endif