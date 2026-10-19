/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef TERMINAL_PROCESS_H
#define TERMINAL_PROCESS_H

#include <stdint.h>

void terminal_process_initialize(void);

int terminal_process_start(uint32_t parent_pid);
void terminal_process_request_exit(void);
int terminal_process_is_running(void);
int terminal_process_consume_redraw(void);
void terminal_process_mark_redraw(void);
void terminal_process_set_input_enabled(int enabled);
int terminal_process_input_enabled(void);
uint32_t terminal_process_pid(void);

#endif
