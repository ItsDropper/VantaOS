/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

#include "vanta_boot_info.h"

/* Bring up platform prerequisites, then invoke subsystem-owned coordinators. */
void kernel_initialize(const vanta_boot_info_t* boot_info);

#endif
