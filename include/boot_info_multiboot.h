/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTA_BOOT_INFO_MULTIBOOT_H
#define VANTA_BOOT_INFO_MULTIBOOT_H

#include "multiboot.h"
#include "vanta_boot_info.h"

/* Converts a Multiboot 1 handoff into the loader-independent boot contract. */
int vanta_boot_info_from_multiboot(
    const multiboot_info_t* multiboot_info,
    vanta_boot_info_t* out_info
);

#endif
