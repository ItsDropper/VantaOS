/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_I2C_H
#define VANTA_I2C_H

#include <stddef.h>
#include <stdint.h>

typedef struct i2c_bus
{
    const char *name;
    void *context;
    int (*transfer)(void *context, uint16_t address, uint32_t speed_hz,
                    const uint8_t *write_data, size_t write_length,
                    uint8_t *read_data, size_t read_length);
    uint8_t ready;
} i2c_bus_t;

/* Enumerates ACPI-described Intel Serial IO I2C controllers and HID children. */
void i2c_initialize(void);

#endif
