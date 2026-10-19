/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef DESKTOP_PROCESS_H
#define DESKTOP_PROCESS_H

#include <stdint.h>

void desktop_process_initialize(void);
void desktop_process_main(void);
int desktop_process_start(uint32_t parent_pid);

#endif
