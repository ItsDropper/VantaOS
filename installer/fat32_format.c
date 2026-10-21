/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "vanta_installer_fat32.h"

#define FAT32_BOOT_SIGNATURE_OFFSET 510U
#define FAT32_FSINFO_SIGNATURE_OFFSET 508U
#define FAT32_FSINFO_LEAD 0x41615252U
#define FAT32_FSINFO_STRUCT 0x61417272U
#define FAT32_FSINFO_TRAIL 0xAA550000U
#define FAT32_ROOT_CLUSTER 2U
#define FAT32_MAX_VOLUME_SECTORS UINT32_MAX

static void zero_bytes(uint8_t *bytes, size_t length)
{
    for (size_t i = 0U; i < length; ++i)
        bytes[i] = 0U;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       size_t length)
{
    for (size_t i = 0U; i < length; ++i)
        destination[i] = source[i];
}

static int bytes_equal(const uint8_t *a, const uint8_t *b, size_t length)
{
    for (size_t i = 0U; i < length; ++i)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0U; i < 4U; ++i)
        p[i] = (uint8_t)(value >> (i * 8U));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static int valid_io(const vanta_installer_disk_io_t *io)
{
    return io && io->read && io->write && io->flush && io->context;
}

static int calculate_geometry(
    uint64_t volume_start_lba,
    uint64_t volume_sector_count,
    vanta_installer_fat32_geometry_t *geometry)
{
    static const uint32_t cluster_sizes[] = {1U, 2U, 4U, 8U, 16U, 32U, 64U};
    for (size_t i = 0U; i < sizeof(cluster_sizes) / sizeof(cluster_sizes[0]); ++i) {
        uint32_t spc = cluster_sizes[i];
        uint64_t fat_sectors = 1U;
        uint64_t clusters = 0U;
        for (unsigned iteration = 0U; iteration < 16U; ++iteration) {
            uint64_t overhead = VANTA_INSTALLER_FAT32_RESERVED_SECTORS +
                                2U * fat_sectors;
            if (overhead >= volume_sector_count)
                break;
            clusters = (volume_sector_count - overhead) / spc;
            if (clusters > VANTA_INSTALLER_FAT32_MAX_CLUSTERS)
                break;
            uint64_t needed = ((clusters + 2U) * 4U + 511U) / 512U;
            if (needed == fat_sectors) {
                if (clusters < VANTA_INSTALLER_FAT32_MIN_CLUSTERS ||
                    clusters > VANTA_INSTALLER_FAT32_MAX_CLUSTERS ||
                    fat_sectors > UINT32_MAX)
                    break;
                geometry->volume_start_lba = volume_start_lba;
                geometry->volume_sector_count = volume_sector_count;
                geometry->sectors_per_cluster = spc;
                geometry->fat_sectors = (uint32_t)fat_sectors;
                geometry->cluster_count = (uint32_t)clusters;
                geometry->root_cluster = FAT32_ROOT_CLUSTER;
                return 1;
            }
            fat_sectors = needed;
        }
    }
    return 0;
}

static void make_boot_sector(
    uint8_t *sector,
    const vanta_installer_fat32_geometry_t *geometry,
    uint32_t volume_id)
{
    zero_bytes(sector, VANTA_INSTALLER_FAT32_SECTOR_SIZE);
    sector[0] = 0xEBU;
    sector[1] = 0x58U;
    sector[2] = 0x90U;
    copy_bytes(sector + 3U, (const uint8_t *)"VANTAOS ", 8U);
    put16(sector + 11U, VANTA_INSTALLER_FAT32_SECTOR_SIZE);
    sector[13] = (uint8_t)geometry->sectors_per_cluster;
    put16(sector + 14U, VANTA_INSTALLER_FAT32_RESERVED_SECTORS);
    sector[16] = 2U;
    put16(sector + 17U, 0U);
    put16(sector + 19U, 0U);
    sector[21] = 0xF8U;
    put16(sector + 22U, 0U);
    put16(sector + 24U, 63U);
    put16(sector + 26U, 255U);
    put32(sector + 28U, (uint32_t)geometry->volume_start_lba);
    put32(sector + 32U, (uint32_t)geometry->volume_sector_count);
    put32(sector + 36U, geometry->fat_sectors);
    put16(sector + 40U, 0U);
    put16(sector + 42U, 0U);
    put32(sector + 44U, geometry->root_cluster);
    put16(sector + 48U, 1U);
    put16(sector + 50U, 6U);
    sector[64] = 0x80U;
    sector[66] = 0x29U;
    put32(sector + 67U, volume_id);
    copy_bytes(sector + 71U, (const uint8_t *)"VANTAOS    ", 11U);
    copy_bytes(sector + 82U, (const uint8_t *)"FAT32   ", 8U);
    put16(sector + FAT32_BOOT_SIGNATURE_OFFSET, 0xAA55U);
}

static void make_fsinfo(uint8_t *sector,
                        const vanta_installer_fat32_geometry_t *geometry)
{
    zero_bytes(sector, VANTA_INSTALLER_FAT32_SECTOR_SIZE);
    put32(sector, FAT32_FSINFO_LEAD);
    put32(sector + 484U, FAT32_FSINFO_STRUCT);
    put32(sector + 488U, geometry->cluster_count - 1U);
    put32(sector + 492U, 3U);
    put32(sector + FAT32_FSINFO_SIGNATURE_OFFSET, FAT32_FSINFO_TRAIL);
}

static int verify_sector(const vanta_installer_disk_io_t *io,
                         uint64_t lba, const uint8_t *expected)
{
    uint8_t actual[VANTA_INSTALLER_FAT32_SECTOR_SIZE];
    return io->read(io->context, lba, 1U, actual) &&
           bytes_equal(actual, expected, sizeof(actual));
}

vanta_installer_fat32_status_t vanta_installer_format_fat32(
    const vanta_installer_disk_io_t *io,
    uint64_t disk_sector_count,
    uint64_t volume_start_lba,
    uint64_t volume_sector_count,
    uint32_t volume_id,
    void *workspace,
    size_t workspace_size,
    vanta_installer_fat32_geometry_t *out_geometry)
{
    vanta_installer_fat32_geometry_t geometry;
    uint8_t boot[VANTA_INSTALLER_FAT32_SECTOR_SIZE];
    uint8_t fsinfo[VANTA_INSTALLER_FAT32_SECTOR_SIZE];
    uint8_t *buffer = (uint8_t *)workspace;
    uint64_t volume_end;
    uint64_t fat1_lba;
    uint64_t fat2_lba;
    uint64_t root_lba;
    uint32_t sectors_per_buffer;

    if (!valid_io(io) || !workspace || !out_geometry ||
        workspace_size < VANTA_INSTALLER_FAT32_WORKSPACE_SIZE)
        return VANTA_INSTALLER_FAT32_INVALID_ARGUMENT;
    if (volume_sector_count < 67584U)
        return VANTA_INSTALLER_FAT32_VOLUME_TOO_SMALL;
    if (volume_start_lba > UINT64_MAX - volume_sector_count)
        return VANTA_INSTALLER_FAT32_UNSUPPORTED_VOLUME;
    volume_end = volume_start_lba + volume_sector_count;
    if (volume_end > disk_sector_count ||
        volume_start_lba > UINT32_MAX ||
        volume_sector_count > FAT32_MAX_VOLUME_SECTORS)
        return VANTA_INSTALLER_FAT32_UNSUPPORTED_VOLUME;
    if (!calculate_geometry(volume_start_lba, volume_sector_count, &geometry))
        return VANTA_INSTALLER_FAT32_UNSUPPORTED_VOLUME;

    fat1_lba = volume_start_lba + VANTA_INSTALLER_FAT32_RESERVED_SECTORS;
    fat2_lba = fat1_lba + geometry.fat_sectors;
    root_lba = fat2_lba + geometry.fat_sectors;
    make_boot_sector(boot, &geometry, volume_id);
    make_fsinfo(fsinfo, &geometry);

    /* Only sectors inside the caller-provided partition range are touched. */
    if (!io->write(io->context, volume_start_lba, 1U, boot) ||
        !io->write(io->context, volume_start_lba + 1U, 1U, fsinfo) ||
        !io->write(io->context, volume_start_lba + 6U, 1U, boot) ||
        !io->write(io->context, volume_start_lba + 7U, 1U, fsinfo))
        return VANTA_INSTALLER_FAT32_IO_ERROR;

    zero_bytes(buffer, VANTA_INSTALLER_FAT32_WORKSPACE_SIZE);
    sectors_per_buffer = (uint32_t)(workspace_size /
                                     VANTA_INSTALLER_FAT32_SECTOR_SIZE);
    if (sectors_per_buffer == 0U)
        return VANTA_INSTALLER_FAT32_INVALID_ARGUMENT;

    for (uint32_t copy = 0U; copy < 2U; ++copy) {
        uint64_t fat_start = copy == 0U ? fat1_lba : fat2_lba;
        uint64_t written = 0U;
        while (written < geometry.fat_sectors) {
            uint64_t remaining = (uint64_t)geometry.fat_sectors - written;
            uint32_t count = remaining < sectors_per_buffer
                ? (uint32_t)remaining : sectors_per_buffer;
            if (!io->write(io->context, fat_start + written, count, buffer))
                return VANTA_INSTALLER_FAT32_IO_ERROR;
            written += count;
        }
    }

    zero_bytes(buffer, VANTA_INSTALLER_FAT32_SECTOR_SIZE);
    put32(buffer, 0x0FFFFFF8U);
    put32(buffer + 4U, 0xFFFFFFFFU);
    put32(buffer + 8U, 0x0FFFFFFFU);
    if (!io->write(io->context, fat1_lba, 1U, buffer) ||
        !io->write(io->context, fat2_lba, 1U, buffer))
        return VANTA_INSTALLER_FAT32_IO_ERROR;

    zero_bytes(buffer, VANTA_INSTALLER_FAT32_SECTOR_SIZE);
    if (!io->write(io->context, root_lba, 1U, buffer) ||
        !io->flush(io->context))
        return VANTA_INSTALLER_FAT32_IO_ERROR;

    if (!verify_sector(io, volume_start_lba, boot) ||
        !verify_sector(io, volume_start_lba + 1U, fsinfo) ||
        !verify_sector(io, volume_start_lba + 6U, boot) ||
        !verify_sector(io, volume_start_lba + 7U, fsinfo))
        return VANTA_INSTALLER_FAT32_VERIFY_ERROR;

    if (!io->read(io->context, fat1_lba, 1U, buffer) ||
        get32(buffer) != 0x0FFFFFF8U ||
        get32(buffer + 4U) != 0xFFFFFFFFU ||
        get32(buffer + 8U) != 0x0FFFFFFFU)
        return VANTA_INSTALLER_FAT32_VERIFY_ERROR;
    if (!io->read(io->context, fat2_lba, 1U, buffer) ||
        get32(buffer) != 0x0FFFFFF8U ||
        get32(buffer + 4U) != 0xFFFFFFFFU ||
        get32(buffer + 8U) != 0x0FFFFFFFU)
        return VANTA_INSTALLER_FAT32_VERIFY_ERROR;

    *out_geometry = geometry;
    return VANTA_INSTALLER_FAT32_OK;
}
