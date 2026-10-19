/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef FILE_EXPLORER_PROCESS_H
#define FILE_EXPLORER_PROCESS_H

#include <stdint.h>

void file_explorer_process_initialize(void);
int file_explorer_process_start(uint32_t parent_pid);
void file_explorer_process_step(void);
uint32_t file_explorer_process_pid(void);

#endif
