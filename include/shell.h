/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef SHELL_H
#define SHELL_H

#include "keyboard.h"
#include "multiboot.h"
#include <stddef.h>

void shell_initialize(void);

void shell_handle_char(char c);

void shell_handle_event(keyboard_event_t event);

void shell_show_prompt(void);
void shell_exec(const char* argument);

/* Execute a command in kernel context and copy its terminal output. */
int shell_execute_kernel_command(const char* command, char* output,
                                 size_t output_capacity);

void shell_set_multiboot_info(multiboot_info_t* mbd);

#endif