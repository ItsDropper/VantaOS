/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef PS2_INPUT_H
#define PS2_INPUT_H
#include <stdbool.h>
#include <stdint.h>
#include "spinlock.h"
bool ps2_controller_initialize(void);
bool ps2_controller_is_ready(void);
bool ps2_keyboard_is_scanning(void);
/* Serializes 8042 status+data access between IRQ handlers and polling. */
extern spinlock_t ps2_io_lock;
/* Service pending 8042 output without depending on IRQ1/IRQ12 delivery. */
void ps2_poll_input(void);
uint32_t ps2_poll_serviced_bytes(void);
uint32_t ps2_input_keyboard_id(void);
uint32_t ps2_input_mouse_id(void);
#endif
