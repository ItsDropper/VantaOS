/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_HID_I2C_H
#define VANTA_HID_I2C_H

#include "i2c.h"
#include <stdint.h>

int hid_i2c_attach(i2c_bus_t *bus, uint16_t address, uint32_t speed_hz,
                   uint16_t hid_descriptor_register, const char *firmware_id);
void hid_i2c_poll(void);

#endif
