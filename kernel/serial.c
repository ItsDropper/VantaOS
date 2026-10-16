#include "serial.h"

#include <stdint.h>
#include <stddef.h>

#define SERIAL_COM1 0x3F8U
#define SERIAL_SPIN_LIMIT 100000U

static inline void serial_out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t serial_in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_initialize(void)
{
    serial_out8(SERIAL_COM1 + 1U, 0x00U);
    serial_out8(SERIAL_COM1 + 3U, 0x80U);
    serial_out8(SERIAL_COM1 + 0U, 0x01U);
    serial_out8(SERIAL_COM1 + 1U, 0x00U);
    serial_out8(SERIAL_COM1 + 3U, 0x03U);
    serial_out8(SERIAL_COM1 + 2U, 0xC7U);
    serial_out8(SERIAL_COM1 + 4U, 0x0BU);
}

static void serial_write_char(char value)
{
    for (uint32_t spin = 0; spin < SERIAL_SPIN_LIMIT; spin++)
    {
        if (serial_in8(SERIAL_COM1 + 5U) & 0x20U)
        {
            serial_out8(SERIAL_COM1, (uint8_t)value);
            return;
        }
    }
}

void serial_write(const char* text)
{
    if (!text)
        return;

    while (*text)
    {
        if (*text == '\n')
            serial_write_char('\r');

        serial_write_char(*text++);
    }
}


void serial_write_bytes(const char* data, size_t length)
{
    if (!data)
        return;

    for (size_t i = 0; i < length; i++)
    {
        if (data[i] == '\n')
            serial_write_char('\r');
        serial_write_char(data[i]);
    }
}
