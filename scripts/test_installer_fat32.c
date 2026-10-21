/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vanta_installer_fat32.h"

#define TEST_DISK_SECTORS 1200000ULL
#define TEST_VOLUME_START 2048ULL
#define TEST_VOLUME_SECTORS 1048576ULL
#define STORED_SECTORS 16U

typedef struct {
    uint64_t lba;
    uint8_t bytes[VANTA_INSTALLER_FAT32_SECTOR_SIZE];
    int used;
} stored_sector_t;

typedef struct {
    uint64_t disk_sectors;
    uint64_t volume_start;
    uint64_t volume_sectors;
    stored_sector_t stored[STORED_SECTORS];
    uint64_t write_calls;
    uint64_t written_sectors;
    uint64_t flush_calls;
    int out_of_bounds_write;
} fake_disk_t;

static int sector_is_zero(const uint8_t *bytes)
{
    for (size_t i = 0U; i < VANTA_INSTALLER_FAT32_SECTOR_SIZE; ++i)
        if (bytes[i] != 0U)
            return 0;
    return 1;
}

static stored_sector_t *find_sector(fake_disk_t *disk, uint64_t lba,
                                    int create)
{
    stored_sector_t *empty = NULL;
    for (size_t i = 0U; i < STORED_SECTORS; ++i) {
        if (disk->stored[i].used && disk->stored[i].lba == lba)
            return &disk->stored[i];
        if (!disk->stored[i].used && !empty)
            empty = &disk->stored[i];
    }
    if (create && empty) {
        empty->used = 1;
        empty->lba = lba;
        memset(empty->bytes, 0, sizeof(empty->bytes));
        return empty;
    }
    return NULL;
}

static int fake_read(void *context, uint64_t lba, uint32_t count, void *buffer)
{
    fake_disk_t *disk = (fake_disk_t *)context;
    uint8_t *out = (uint8_t *)buffer;
    if (!disk || !buffer || count == 0U || lba > disk->disk_sectors ||
        (uint64_t)count > disk->disk_sectors - lba)
        return 0;
    for (uint32_t i = 0U; i < count; ++i) {
        stored_sector_t *stored = find_sector(disk, lba + i, 0);
        if (stored)
            memcpy(out + (size_t)i * VANTA_INSTALLER_FAT32_SECTOR_SIZE,
                   stored->bytes, VANTA_INSTALLER_FAT32_SECTOR_SIZE);
        else
            memset(out + (size_t)i * VANTA_INSTALLER_FAT32_SECTOR_SIZE, 0,
                   VANTA_INSTALLER_FAT32_SECTOR_SIZE);
    }
    return 1;
}

static int fake_write(void *context, uint64_t lba, uint32_t count,
                      const void *buffer)
{
    fake_disk_t *disk = (fake_disk_t *)context;
    const uint8_t *in = (const uint8_t *)buffer;
    if (!disk || !buffer || count == 0U || lba > disk->disk_sectors ||
        (uint64_t)count > disk->disk_sectors - lba)
        return 0;
    disk->write_calls++;
    disk->written_sectors += count;
    for (uint32_t i = 0U; i < count; ++i) {
        uint64_t current = lba + i;
        const uint8_t *source =
            in + (size_t)i * VANTA_INSTALLER_FAT32_SECTOR_SIZE;
        if (current < disk->volume_start ||
            current >= disk->volume_start + disk->volume_sectors)
            disk->out_of_bounds_write = 1;
        if (sector_is_zero(source))
            continue;
        stored_sector_t *stored = find_sector(disk, current, 1);
        if (!stored)
            return 0;
        memcpy(stored->bytes, source, VANTA_INSTALLER_FAT32_SECTOR_SIZE);
    }
    return 1;
}

static int fake_flush(void *context)
{
    fake_disk_t *disk = (fake_disk_t *)context;
    if (!disk)
        return 0;
    disk->flush_calls++;
    return 1;
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

int main(void)
{
    fake_disk_t disk;
    vanta_installer_disk_io_t io;
    vanta_installer_fat32_geometry_t geometry;
    uint8_t workspace[VANTA_INSTALLER_FAT32_WORKSPACE_SIZE];
    uint8_t boot[VANTA_INSTALLER_FAT32_SECTOR_SIZE];
    uint8_t fsinfo[VANTA_INSTALLER_FAT32_SECTOR_SIZE];
    uint8_t fat[VANTA_INSTALLER_FAT32_SECTOR_SIZE];
    uint64_t fat1_lba;
    uint64_t fat2_lba;
    uint64_t root_lba;

    memset(&disk, 0, sizeof(disk));
    disk.disk_sectors = TEST_DISK_SECTORS;
    disk.volume_start = TEST_VOLUME_START;
    disk.volume_sectors = TEST_VOLUME_SECTORS;
    io.read = fake_read;
    io.write = fake_write;
    io.flush = fake_flush;
    io.context = &disk;

    if (vanta_installer_format_fat32(
            &io, TEST_DISK_SECTORS, TEST_VOLUME_START, 65536U, 7U,
            workspace, sizeof(workspace), &geometry) !=
        VANTA_INSTALLER_FAT32_VOLUME_TOO_SMALL ||
        disk.write_calls != 0U) {
        fprintf(stderr, "small-volume rejection failed\n");
        return 1;
    }

    if (vanta_installer_format_fat32(
            &io, TEST_DISK_SECTORS, TEST_VOLUME_START, TEST_VOLUME_SECTORS,
            0x56414E54U, workspace, sizeof(workspace), &geometry) !=
        VANTA_INSTALLER_FAT32_OK) {
        fprintf(stderr, "FAT32 formatter failed\n");
        return 1;
    }

    if (disk.out_of_bounds_write || disk.flush_calls == 0U ||
        disk.written_sectors <= geometry.fat_sectors * 2ULL ||
        geometry.cluster_count < VANTA_INSTALLER_FAT32_MIN_CLUSTERS ||
        geometry.cluster_count > VANTA_INSTALLER_FAT32_MAX_CLUSTERS ||
        geometry.sectors_per_cluster == 0U ||
        (geometry.sectors_per_cluster & (geometry.sectors_per_cluster - 1U)) != 0U) {
        fprintf(stderr, "invalid formatter geometry or write bounds\n");
        return 1;
    }

    if (!fake_read(&disk, TEST_VOLUME_START, 1U, boot) ||
        !fake_read(&disk, TEST_VOLUME_START + 1U, 1U, fsinfo) ||
        boot[510] != 0x55U || boot[511] != 0xAAU ||
        boot[82] != 'F' || boot[83] != 'A' || boot[84] != 'T' ||
        boot[13] != geometry.sectors_per_cluster ||
        read32(boot + 36U) != geometry.fat_sectors ||
        read32(boot + 44U) != 2U ||
        read32(fsinfo) != 0x41615252U ||
        read32(fsinfo + 484U) != 0x61417272U ||
        read32(fsinfo + 488U) != geometry.cluster_count - 1U ||
        read32(fsinfo + 492U) != 3U ||
        read32(fsinfo + 508U) != 0xAA550000U) {
        fprintf(stderr, "FAT32 boot sector or FSInfo mismatch\n");
        return 1;
    }

    fat1_lba = TEST_VOLUME_START + VANTA_INSTALLER_FAT32_RESERVED_SECTORS;
    fat2_lba = fat1_lba + geometry.fat_sectors;
    root_lba = fat2_lba + geometry.fat_sectors;
    if (!fake_read(&disk, fat1_lba, 1U, fat) ||
        read32(fat) != 0x0FFFFFF8U ||
        read32(fat + 4U) != 0xFFFFFFFFU ||
        read32(fat + 8U) != 0x0FFFFFFFU ||
        !fake_read(&disk, fat2_lba, 1U, fat) ||
        read32(fat) != 0x0FFFFFF8U ||
        read32(fat + 4U) != 0xFFFFFFFFU ||
        read32(fat + 8U) != 0x0FFFFFFFU ||
        !fake_read(&disk, root_lba, 1U, fat) || !sector_is_zero(fat)) {
        fprintf(stderr, "FAT copies or root cluster initialization failed\n");
        return 1;
    }

    printf("VANTAOS_INSTALLER_FAT32_OK\n");
    return 0;
}
