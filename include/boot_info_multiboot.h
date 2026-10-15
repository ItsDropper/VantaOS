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
