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
bool ps2_controller_initialize(void);
bool ps2_controller_is_ready(void);
uint32_t ps2_input_keyboard_id(void);
uint32_t ps2_input_mouse_id(void);
#endif
