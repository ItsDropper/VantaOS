/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stddef.h>
#include <stdint.h>

#define MULTIBOOT_MEMORY_AVAILABLE 1
#define MULTIBOOT_MEMORY_RESERVED  2

#define MULTIBOOT_INFO_MEM_MAP     (1 << 6)
#define MULTIBOOT_INFO_FRAMEBUFFER (1 << 12)

typedef struct multiboot_mmap_entry
{
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed)) multiboot_memory_map_t;

typedef struct multiboot_info
{
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t num;
    uint32_t size;
    uint32_t addr;
    uint32_t shndx;
    uint32_t mmap_length;
    uint32_t mmap_addr;

    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;

    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;

    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint8_t framebuffer_red_position;
    uint8_t framebuffer_red_mask_size;
    uint8_t framebuffer_green_position;
    uint8_t framebuffer_green_mask_size;
    uint8_t framebuffer_blue_position;
    uint8_t framebuffer_blue_mask_size;
} __attribute__((packed)) multiboot_info_t;

_Static_assert(offsetof(multiboot_info_t, mmap_length) == 44, "Multiboot mmap_length offset");
_Static_assert(offsetof(multiboot_info_t, mmap_addr) == 48, "Multiboot mmap_addr offset");
_Static_assert(offsetof(multiboot_info_t, framebuffer_addr) == 88, "Multiboot framebuffer offset");

#endif
