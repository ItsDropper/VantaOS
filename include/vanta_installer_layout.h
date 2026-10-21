/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTA_INSTALLER_LAYOUT_H
#define VANTA_INSTALLER_LAYOUT_H

#include <stdint.h>

#define VANTA_INSTALLER_SECTOR_SIZE 512U
#define VANTA_INSTALLER_ALIGNMENT_SECTORS 2048ULL
#define VANTA_INSTALLER_ESP_SECTORS 1048576ULL
#define VANTA_INSTALLER_MIN_DISK_SECTORS 8388608ULL
#define VANTA_INSTALLER_GPT_RESERVED_SECTORS 34ULL

typedef enum {
    VANTA_INSTALLER_LAYOUT_OK = 0,
    VANTA_INSTALLER_LAYOUT_INVALID_ARGUMENT,
    VANTA_INSTALLER_LAYOUT_UNSUPPORTED_SECTOR_SIZE,
    VANTA_INSTALLER_LAYOUT_DISK_TOO_SMALL,
    VANTA_INSTALLER_LAYOUT_OVERFLOW,
    VANTA_INSTALLER_LAYOUT_OUT_OF_BOUNDS
} vanta_installer_layout_status_t;

typedef struct {
    uint64_t first_lba;
    uint64_t sector_count;
} vanta_installer_partition_t;

typedef struct {
    uint64_t disk_sector_count;
    vanta_installer_partition_t efi;
    vanta_installer_partition_t system;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
} vanta_installer_layout_t;

/*
 * Pure planner only: this function performs no I/O and cannot modify a disk.
 * The first milestone deliberately supports 512-byte logical sectors only.
 */
vanta_installer_layout_status_t vanta_installer_plan_gpt(
    uint64_t disk_sector_count,
    uint32_t logical_sector_size,
    vanta_installer_layout_t *out_layout);

#endif
