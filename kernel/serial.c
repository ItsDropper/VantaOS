/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "serial.h"
#include "serial_log.h"
#ifdef VANTAOS_BOOT_LOG_TO_FAT32
#include "fat32.h"
#endif

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
#ifdef VANTAOS_BOOT_LOG_TO_FAT32
    size_t log_length = 0U;
    while (text[log_length]) ++log_length;
    serial_log_append(text, log_length);
#endif

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
#ifdef VANTAOS_BOOT_LOG_TO_FAT32
    serial_log_append(data, length);
#endif

    for (size_t i = 0; i < length; i++)
    {
        if (data[i] == '\n')
            serial_write_char('\r');
        serial_write_char(data[i]);
    }
}

int serial_boot_log_flush(void)
{
#ifdef VANTAOS_BOOT_LOG_TO_FAT32
    static char snapshot[SERIAL_LOG_CAPACITY];
    static int directory_attempted;
    if (!fat32_is_mounted())
        return 0;
    if (!directory_attempted) {
        (void)fat32_create_directory("/LOGS");
        directory_attempted = 1;
    }
    size_t length = serial_log_snapshot(snapshot, sizeof(snapshot));
    if (length == 0U)
        return 1;
    return fat32_write_file("/LOGS/BOOT.TXT", snapshot, (uint32_t)length) == 0;
#else
    return 0;
#endif
}
