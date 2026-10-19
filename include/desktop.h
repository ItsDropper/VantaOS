/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef DESKTOP_H
#define DESKTOP_H

#include "multiboot.h"

void desktop_initialize(multiboot_info_t* mbd);
void desktop_present(void);
void desktop_update(void);
int desktop_launch_terminal(void);

#endif
