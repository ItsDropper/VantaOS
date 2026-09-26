#include "terminal.h"

static volatile unsigned short* video_memory =
    (volatile unsigned short*)0xB8000;

static int cursor = 0;

static void terminal_clear(void)
{
    for (int i = 0; i < 80 * 25; i++)
    {
        video_memory[i] = ' ' | (0x0F << 8);
    }

    cursor = 0;
}

void terminal_initialize(void)
{
    terminal_clear();

    terminal_write("VantaOS");
    terminal_putchar('\n');
    terminal_putchar('>');
}

void terminal_putchar(char c)
{
    if (c == '\n')
    {
        cursor = ((cursor / 80) + 1) * 80;
        return;
    }

    video_memory[cursor] = (unsigned short)c | (0x0F << 8);
    cursor++;

    if (cursor >= 80 * 25)
        cursor = 0;
}

void terminal_backspace(void)
{
    if (cursor > 8)
    {
        cursor--;
        video_memory[cursor] = ' ' | (0x0F << 8);
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

void terminal_write_hex(unsigned int value)
{
    const char* hex = "0123456789ABCDEF";

    terminal_write("0x");

    for (int i = 7; i >= 0; i--)
    {
        terminal_putchar(hex[(value >> (i * 4)) & 0xF]);
    }
}
