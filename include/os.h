/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef OS_H
#define OS_H

#include "multiboot.h"

void os_initialize(multiboot_info_t* mbd);
void os_run(void);

#endif
