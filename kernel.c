#include "terminal.h"
#include "keyboard.h"
#include "interrupts.h"
#include "gdt.h"

void kernel_main(void)
{
    terminal_initialize();

    gdt_initialize();

    keyboard_initialize();
    interrupts_initialize();

    __asm__ volatile ("sti");

    while (1)
    {
        char c = keyboard_get_char();

        if (c == 0)
            continue;

        if (c == '\b')
        {
            terminal_backspace();
        }
        else
        {
            terminal_putchar(c);
        }
    }
}