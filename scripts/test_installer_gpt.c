/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "vanta_installer_gpt.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define FAKE_SECTOR_RECORDS 67U

typedef struct {
    uint64_t lba;
    uint8_t bytes[VANTA_INSTALLER_SECTOR_SIZE];
    int used;
} fake_sector_t;

typedef struct {
    uint64_t sector_count;
    fake_sector_t sectors[FAKE_SECTOR_RECORDS];
    unsigned writes;
    unsigned fail_write_number;
    unsigned flushes;
} fake_disk_t;

static fake_sector_t *find_sector(fake_disk_t *disk, uint64_t lba, int create)
{
    fake_sector_t *free_slot = NULL;
    for (size_t i = 0U; i < FAKE_SECTOR_RECORDS; ++i) {
        if (disk->sectors[i].used && disk->sectors[i].lba == lba)
            return &disk->sectors[i];
        if (!disk->sectors[i].used && !free_slot)
            free_slot = &disk->sectors[i];
    }
    if (!create || !free_slot)
        return NULL;
    free_slot->used = 1;
    free_slot->lba = lba;
    for (size_t i = 0U; i < VANTA_INSTALLER_SECTOR_SIZE; ++i)
        free_slot->bytes[i] = 0U;
    return free_slot;
}

static int fake_read(void *context, uint64_t lba, uint32_t count, void *buffer)
{
    fake_disk_t *disk = (fake_disk_t *)context;
    uint8_t *out = (uint8_t *)buffer;
    if (!disk || !buffer || count == 0U || lba >= disk->sector_count ||
        (uint64_t)count > disk->sector_count - lba)
        return 0;
    for (uint32_t i = 0U; i < count; ++i) {
        fake_sector_t *sector = find_sector(disk, lba + i, 0);
        if (sector) {
            for (size_t j = 0U; j < VANTA_INSTALLER_SECTOR_SIZE; ++j)
                out[(size_t)i * VANTA_INSTALLER_SECTOR_SIZE + j] = sector->bytes[j];
        } else {
            for (size_t j = 0U; j < VANTA_INSTALLER_SECTOR_SIZE; ++j)
                out[(size_t)i * VANTA_INSTALLER_SECTOR_SIZE + j] = 0U;
        }
    }
    return 1;
}

static int fake_write(void *context, uint64_t lba, uint32_t count,
                      const void *buffer)
{
    fake_disk_t *disk = (fake_disk_t *)context;
    const uint8_t *in = (const uint8_t *)buffer;
    if (!disk || !buffer || count == 0U || lba >= disk->sector_count ||
        (uint64_t)count > disk->sector_count - lba)
        return 0;
    ++disk->writes;
    if (disk->fail_write_number != 0U &&
        disk->writes == disk->fail_write_number)
        return 0;
    for (uint32_t i = 0U; i < count; ++i) {
        fake_sector_t *sector = find_sector(disk, lba + i, 1);
        if (!sector)
            return 0;
        for (size_t j = 0U; j < VANTA_INSTALLER_SECTOR_SIZE; ++j)
            sector->bytes[j] = in[(size_t)i * VANTA_INSTALLER_SECTOR_SIZE + j];
    }
    return 1;
}

static int fake_flush(void *context)
{
    fake_disk_t *disk = (fake_disk_t *)context;
    if (!disk)
        return 0;
    ++disk->flushes;
    return 1;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t value = 0U;
    for (unsigned i = 0U; i < 8U; ++i)
        value |= (uint64_t)p[i] << (i * 8U);
    return value;
}

static int has_signature(const uint8_t *sector)
{
    static const uint8_t signature[8] = {
        'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'
    };
    for (size_t i = 0U; i < sizeof(signature); ++i)
        if (sector[i] != signature[i])
            return 0;
    return 1;
}

static void test_writer_and_readback(void)
{
    fake_disk_t disk = {0};
    vanta_installer_layout_t layout;
    uint8_t workspace[VANTA_INSTALLER_GPT_WORKSPACE_SIZE];
    const uint8_t disk_guid[16] =
        {1U,2U,3U,4U,5U,6U,7U,8U,9U,10U,11U,12U,13U,14U,15U,16U};
    const uint8_t efi_guid[16] =
        {17U,18U,19U,20U,21U,22U,23U,24U,25U,26U,27U,28U,29U,30U,31U,32U};
    const uint8_t system_guid[16] =
        {33U,34U,35U,36U,37U,38U,39U,40U,41U,42U,43U,44U,45U,46U,47U,48U};
    vanta_installer_disk_io_t io = {
        fake_read, fake_write, fake_flush, &disk
    };
    const uint64_t disk_size = VANTA_INSTALLER_MIN_DISK_SECTORS;
    uint8_t sector[VANTA_INSTALLER_SECTOR_SIZE];

    disk.sector_count = disk_size;
    assert(vanta_installer_plan_gpt(
        disk_size, VANTA_INSTALLER_SECTOR_SIZE, &layout) ==
        VANTA_INSTALLER_LAYOUT_OK);
    assert(vanta_installer_write_gpt(
        &io, &layout, disk_guid, efi_guid, system_guid,
        workspace, sizeof(workspace)) == VANTA_INSTALLER_GPT_OK);
    assert(disk.flushes == 2U);
    assert(fake_read(&disk, 0U, 1U, sector));
    assert(sector[510U] == 0x55U && sector[511U] == 0xAAU);
    assert(sector[450U] == 0xEEU);
    assert(fake_read(&disk, 1U, 1U, sector));
    assert(has_signature(sector));
    assert(get32(sector + 12U) == 92U);
    assert(get64(sector + 72U) == 2U);
    assert(get32(sector + 80U) == VANTA_INSTALLER_GPT_ENTRY_COUNT);
    assert(get32(sector + 84U) == VANTA_INSTALLER_GPT_ENTRY_SIZE);
    assert(fake_read(&disk, 2U, 1U, sector));
    assert(get64(sector + 32U) == layout.efi.first_lba);
    assert(get64(sector + 40U) ==
           layout.efi.first_lba + layout.efi.sector_count - 1U);
    /* Both 128-byte GPT entries occupy the first 512-byte entry-array sector. */
    assert(get64(sector + 128U + 32U) == layout.system.first_lba);
    assert(get64(sector + 128U + 40U) ==
           layout.system.first_lba + layout.system.sector_count - 1U);
    assert(fake_read(&disk, disk_size - 1U, 1U, sector));
    assert(has_signature(sector));
    assert(get64(sector + 24U) == disk_size - 1U);
    assert(get64(sector + 32U) == 1U);

    /* The writer must also accept the smaller supported ESP layout. */
    disk = (fake_disk_t){0};
    disk.sector_count = disk_size;
    assert(vanta_installer_plan_gpt_with_efi_size(
        disk_size, VANTA_INSTALLER_SECTOR_SIZE, 256U, &layout) ==
        VANTA_INSTALLER_LAYOUT_OK);
    assert(layout.efi.sector_count == 256ULL * 2048ULL);
    assert(vanta_installer_write_gpt(
        &io, &layout, disk_guid, efi_guid, system_guid,
        workspace, sizeof(workspace)) == VANTA_INSTALLER_GPT_OK);
}

static void test_rejects_duplicate_guids_and_short_workspace(void)
{
    fake_disk_t disk = {0};
    vanta_installer_layout_t layout;
    uint8_t workspace[VANTA_INSTALLER_GPT_WORKSPACE_SIZE];
    const uint8_t guid[16] =
        {1U,2U,3U,4U,5U,6U,7U,8U,9U,10U,11U,12U,13U,14U,15U,16U};
    const uint8_t other[16] =
        {17U,18U,19U,20U,21U,22U,23U,24U,25U,26U,27U,28U,29U,30U,31U,32U};
    vanta_installer_disk_io_t io = {
        fake_read, fake_write, fake_flush, &disk
    };
    disk.sector_count = VANTA_INSTALLER_MIN_DISK_SECTORS;
    assert(vanta_installer_plan_gpt(
        disk.sector_count, VANTA_INSTALLER_SECTOR_SIZE, &layout) ==
        VANTA_INSTALLER_LAYOUT_OK);
    assert(vanta_installer_write_gpt(
        &io, &layout, guid, guid, other, workspace, sizeof(workspace)) ==
        VANTA_INSTALLER_GPT_INVALID_ARGUMENT);
    assert(vanta_installer_write_gpt(
        &io, &layout, guid, other, guid, workspace, sizeof(workspace)) ==
        VANTA_INSTALLER_GPT_INVALID_ARGUMENT);
    assert(vanta_installer_write_gpt(
        &io, &layout, guid, other, guid, workspace,
        sizeof(workspace) - 1U) == VANTA_INSTALLER_GPT_INVALID_ARGUMENT);
    assert(disk.writes == 0U);
}

static void test_reports_write_failure(void)
{
    fake_disk_t disk = {0};
    vanta_installer_layout_t layout;
    uint8_t workspace[VANTA_INSTALLER_GPT_WORKSPACE_SIZE];
    const uint8_t disk_guid[16] =
        {1U,2U,3U,4U,5U,6U,7U,8U,9U,10U,11U,12U,13U,14U,15U,16U};
    const uint8_t efi_guid[16] =
        {17U,18U,19U,20U,21U,22U,23U,24U,25U,26U,27U,28U,29U,30U,31U,32U};
    const uint8_t system_guid[16] =
        {33U,34U,35U,36U,37U,38U,39U,40U,41U,42U,43U,44U,45U,46U,47U,48U};
    vanta_installer_disk_io_t io = {
        fake_read, fake_write, fake_flush, &disk
    };
    disk.sector_count = VANTA_INSTALLER_MIN_DISK_SECTORS;
    disk.fail_write_number = 1U;
    assert(vanta_installer_plan_gpt(
        disk.sector_count, VANTA_INSTALLER_SECTOR_SIZE, &layout) ==
        VANTA_INSTALLER_LAYOUT_OK);
    assert(vanta_installer_write_gpt(
        &io, &layout, disk_guid, efi_guid, system_guid,
        workspace, sizeof(workspace)) == VANTA_INSTALLER_GPT_IO_ERROR);
}

int main(void)
{
    test_writer_and_readback();
    test_rejects_duplicate_guids_and_short_workspace();
    test_reports_write_failure();
    puts("VANTAOS_INSTALLER_GPT_OK");
    return 0;
}
