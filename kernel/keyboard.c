#include "keyboard.h"

#define KEYBOARD_BUFFER_SIZE 128

static const char keyboard_map[] = {
    0,  0,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '-', '=', 0,  0,  'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', 0,  0, 'a', 's', 'd', 'f', 'g', 'h',
    'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

static volatile char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile unsigned int buffer_read = 0;
static volatile unsigned int buffer_write = 0;

static unsigned char keyboard_read_scancode(void)
{
    unsigned char scancode;

    __asm__ volatile (
        "inb %%dx, %%al"
        : "=a"(scancode)
        : "d"(0x60)
    );

    return scancode;
}

static void keyboard_buffer_push(char c)
{
    unsigned int next = (buffer_write + 1) % KEYBOARD_BUFFER_SIZE;

    if (next == buffer_read)
        return;

    keyboard_buffer[buffer_write] = c;
    buffer_write = next;
}

void keyboard_initialize(void)
{
    buffer_read = 0;
    buffer_write = 0;
}

void keyboard_handle_interrupt(void)
{
    unsigned char scancode = keyboard_read_scancode();

    // Ignore key releases.
    if (scancode & 0x80)
        return;

    // Backspace.
    if (scancode == 0x0E)
    {
        keyboard_buffer_push('\b');
        return;
    }

    // Enter.
    if (scancode == 0x1C)
    {
        keyboard_buffer_push('\n');
        return;
    }

    if (scancode < sizeof(keyboard_map))
    {
        char c = keyboard_map[scancode];

        if (c)
            keyboard_buffer_push(c);
    }
}

char keyboard_get_char(void)
{
    if (buffer_read == buffer_write)
        return 0;

    char c = keyboard_buffer[buffer_read];

    buffer_read = (buffer_read + 1) % KEYBOARD_BUFFER_SIZE;

    return c;
}