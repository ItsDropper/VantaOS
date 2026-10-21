/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTA_INSTALLER_FAT32_H
#define VANTA_INSTALLER_FAT32_H

#include <stddef.h>
#include <stdint.h>

#include "vanta_installer_gpt.h"

#define VANTA_INSTALLER_FAT32_SECTOR_SIZE 512U
#define VANTA_INSTALLER_FAT32_RESERVED_SECTORS 32U
#define VANTA_INSTALLER_FAT32_MIN_CLUSTERS 65525U
#define VANTA_INSTALLER_FAT32_MAX_CLUSTERS 0x0FFFFFF5U
#define VANTA_INSTALLER_FAT32_WORKSPACE_SIZE 65536U

typedef enum {
    VANTA_INSTALLER_FAT32_OK = 0,
    VANTA_INSTALLER_FAT32_INVALID_ARGUMENT,
    VANTA_INSTALLER_FAT32_VOLUME_TOO_SMALL,
    VANTA_INSTALLER_FAT32_UNSUPPORTED_VOLUME,
    VANTA_INSTALLER_FAT32_IO_ERROR,
    VANTA_INSTALLER_FAT32_VERIFY_ERROR
} vanta_installer_fat32_status_t;

typedef struct {
    uint64_t volume_start_lba;
    uint64_t volume_sector_count;
    uint32_t sectors_per_cluster;
    uint32_t fat_sectors;
    uint32_t cluster_count;
    uint32_t root_cluster;
} vanta_installer_fat32_geometry_t;

/*
 * Formats one already-selected partition as FAT32 using 512-byte sectors.
 * This is destructive to the supplied partition range. The caller must have
 * independently verified the device identity and obtained explicit user
 * confirmation. It never changes sectors outside the given range.
 *
 * workspace must provide at least VANTA_INSTALLER_FAT32_WORKSPACE_SIZE bytes.
 */
vanta_installer_fat32_status_t vanta_installer_format_fat32(
    const vanta_installer_disk_io_t *io,
    uint64_t disk_sector_count,
    uint64_t volume_start_lba,
    uint64_t volume_sector_count,
    uint32_t volume_id,
    void *workspace,
    size_t workspace_size,
    vanta_installer_fat32_geometry_t *out_geometry);

#endif
